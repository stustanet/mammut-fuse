#!/usr/bin/env python3

"""
the mammutfsd management daemon controls the interaction between different
running instances of mammutfs

It also manages interfacing with different tools that depend on the inotify
messages being sent by the filesystem instances, for example to reload the
anonmap
"""

import asyncio
import json
import logging
import os
import sys

import argparse
from . import libconf

logging.basicConfig(
    level=logging.DEBUG,
    format='%(name)s: %(message)s',
    stream=sys.stderr,
)

class MammutfsdClient:
    # pylint: disable=too-many-instance-attributes
    """
    Represents one single connected mammutfs
    """

    def __init__(self, reader, writer, mfsd, removal_queue):
        self.mfsd = mfsd
        self.details = {}

        queue_limit = 1000
        self._plugin_fileop_queue = asyncio.Queue(loop=self.mfsd.loop,
                                                  maxsize=queue_limit)

        self._request_pending = False
        self._request_queue = asyncio.Queue(loop=self.mfsd.loop,
                                            maxsize=queue_limit)

        self.writer = writer
        self.read_task = self.mfsd.loop.create_task(
            self.readloop(reader, writer, removal_queue))
        self.plugin_call_task = self.mfsd.loop.create_task(
            self.plugin_call_loop())

    async def readloop(self, reader, writer, removal_queue):
        """
        Wait for incoming data, decode it and forward it to the plugins and
        a possible interactive user
        """
        # Hello-loop
        while True:
            line = await reader.readline()
            try:
                data = json.loads(line.decode('utf-8'))
                if 'op' in data and data['op'] == 'hello':
                    self.details = data
                    del self.details['op']
                    self.mfsd.log.info("fs connect. announced user: %s",
                                       self.details['user'])
                    break
                else:
                    self.mfsd.log.warning("Invalid client hello received: %s", line)
                    writer.close()
                    await writer.drain()
            except json.JSONDecodeError:
                self.mfsd.log.warning("Hello: invalid data received: " + str(line))
                writer.close()
                await writer.drain()

        # Working loop
        try:
            while True:
                line = await reader.readline()
                #self.mfsd.info("read line: " + str(line))
                if not line:
                    # We should die - this could be tricky
                    # 1. This loop must die   < done here by returning
                    # 2. This task mus be removed from the running clients
                    # 2. This task must die   < done here by returning
                    # 3. This task mus be read
                    break

                try:
                    data = json.loads(line.decode('utf-8'))
                except json.JSONDecodeError:
                    self.mfsd.log.warn("Invalid data received: " + str(line))
                    continue

                if self._request_pending:
                    # A request can intercept the next flying message
                    # this might actually lead to races, when a file is
                    # changed at the same time
                    await self._request_queue.put(data)
                else:
                    await self.process_message(data)
        finally:
            removal_queue.put_nowait(self)
            self.mfsd.log.info("fs client disconnect")


    async def process_message(self, data):
        """
        Decode a received message from a mammutfs instance
        """
        if 'state' in data:
            # This is a state message!
            await self.mfsd.global_write(json.dumps(data))
        elif 'op' in data and 'module' in data:
            await self.mfsd.global_write(json.dumps(data))
            # Dispatch plugin calls to another coroutine
            await self._plugin_fileop_queue.put(data)
        elif 'event' in data and data['event'] == 'namechange':
            # Handle events
            await self.mfsd.name_change(data)
        else:
            self.mfsd.log.warning("Unknown data received: " + str(data))


    async def plugin_call_loop(self):
        """
        Plugin calls should not be run in the scope if the readloop,
        because plugins would not be able to send commands to a mammutfs, since
        the loop is busy doing this request
        Therefore this loops only purpose is to call plugins
        """
        while True:
            data = await self._plugin_fileop_queue.get()
            try:
                await asyncio.wait_for(
                    self.mfsd.call_plugin('on_fileop', self, data, writer=None),
                    5)
            except asyncio.TimeoutError:
                print("Some plugin has timed out. This is bad!")


    async def request(self, command):
        """
        Send the command to the mammutfs and wait for a response
        """
        self._request_pending = True
        await self.write(command)
        response = await self._request_queue.get()
        self._request_pending = False
        return response


    async def write(self, command):
        """
        Send the command to the mammutfs and return immediately
        """
        try:
            self.writer.write(command.encode('utf-8'))
            await self.writer.drain()
        except ConnectionResetError:
            await self.close()


    async def close(self):
        """
        Close the running tasks and tear it down and clean it
        """
        try:
            self.read_task.cancel()
            await self.read_task
        except asyncio.CancelledError:
            pass
        try:
            self.plugin_call_task.cancel()
            await self.plugin_call_task
        except asyncio.CancelledError:
            pass


    async def anonym_raid(self):
        """
        Request the raid, where the anonymous folder is located at.
        Send a request to the client, if neccessary.
        """
        if 'anonym_raid' in self.details:
            return self.details['anonym_raid']
        else:
            response = await self.request("anonym_raid")
            if response['state'] == 'success':
                raid = response['response']
                self.details['anonym_raid'] = raid
                return raid
        return ""


    async def user(self):
        """
        Request the username
        send a request to the client if neccessary
        """
        if 'user' in self.details:
            return self.details['user']
        else:
            # Request config from the connected mammutfs
            response = self.request("user")
            if response['state'] == 'success':
                user = response['response']
                self.details['user'] = user
                return user
        return ""


class MammutfsDaemon:
    """
    Mammut file system administration daemon core

    possible plugin options (all are couroutines):

    init(mammutfsdaemon) # REQUIRED, returns the plugin object
                         # arg: self
    teardown() # destroy the class again
    on_client(clientsocket) # Called when a new client is connecting
                            # @arg: the socket connection to this client
    on_fileop(client, jsonop) # Called when a file operation is happening
                              # @arg json: the change json dict
                              # @arg client: the client that sent this

    plugins can also register callbacks to interactive commands here using

    mammutfsd.register("Command", async callback), whereby the callback receives
    command.split(' ') for possible command line args
    """

    def __init__(self, configfile, loop, **kwargs):
        with open(configfile) as cfgf:
            self.config = libconf.load(cfgf)

        for key, value in kwargs.items():
            self.config['mammutfsd'][key] = value

        self.log = logging.getLogger('mammutfsd')
        self._commands = {}
        self._plugins = []
        self._clients = []

        self._writers = []

        if not loop:
            loop = asyncio.get_event_loop()
        self.loop = loop

        self._interactionsocket = loop.create_task(self.interactionsocket())
        self._clientmanagementtask = loop.create_task(self.client_management())

        loop.run_until_complete(self._load_plugins())

        self.register("kill_yourself", self.cmd_shutdown)

    async def cmd_shutdown(self, client, writer, cmd):
        writer.write(b"Stopping the daemon. goodbye")
        await writer.drain()
        self.loop.stop()

    async def client_management(self):
        """
        An client will insert itself into here if it wants to die as some kind
        of suicide booth
        """

        client_removal_queue = asyncio.Queue(loop=self.loop)

        server = await asyncio.start_unix_server(
            lambda r, w: self.client_connected(r, w, client_removal_queue),
            loop=self.loop,
            path=self.config['daemon_socket'])

        # now we have to change the sockets permissions to 0777 - so mammutfs
        # can actually connect
        os.chmod(self.config['daemon_socket'], 0o777)

        try:
            while True:
                to_remove = await client_removal_queue.get()
                self._clients = [c for c in self._clients if c != to_remove]
                await to_remove.close()
        finally:
            server.close()


    async def client_connected(self, reader, writer, removal_queue):
        """
        Called whenever a new mammutfs connected
        """
        client = MammutfsdClient(reader, writer, self, removal_queue)
        self._clients.append(client)
        await self.call_plugin('on_client', client, {}, writer=None)


    def register(self, command, callback):
        """
        Register a interactive command to the given callback
        Multiple commands can be registered to the same string.
        The order is not defined in which order they will be called.
        """
        try:
            self._commands[command].append(callback)
        except KeyError:
            self._commands[command] = [callback]


    async def _load_plugins(self):
        """
        Read the list from the configfile and import the modules
        """
        cfg_plugins = list(self.config["mammutfsd"]["plugins"])

        mfsd = __import__("mammutfsd")
        for pluginname in cfg_plugins + ['mammutfsd_help']:
            try:
                __import__("mammutfsd." + pluginname)
                module = getattr(mfsd, pluginname)
                self._plugins.append(await module.init(self.loop, self))
            except AttributeError as exp:
                # The plugin did not have a teardown method
                self.log.info("Attribute Error")
                self.log.exception(exp)
            except ImportError as e:
                self.log.error("Could not load plugin: %s", pluginname)
                self.log.error(e)


    async def call_plugin(self, function, client, args, writer=None):
        """
        Call the defined function (as string) on all plugins.
        client and args are the arguments that will be forwarded to the function
        """
        for plugin in self._plugins:
            try:
                func = getattr(plugin, function)
            except AttributeError:
                # The plugin did not have the method
                continue

            if writer is None:
                async def noop():
                    return
                writer = type("GlobalWriter", (object,), {
                    "write": self.global_write,
                    "drain": noop
                })

            if asyncio.iscoroutinefunction(func):
                await func(client, writer, args)
            else:
                func(client, writer, args)
            await writer.drain()

    async def name_change(self, data):
        """
        Handle events that come from the mammutfs that do not have the source
        of public file listing related files, such as display name changes
        """
        if data['event'] == 'namechange' and "source" in data and "dest" in data:
            await self.call_plugin('on_namechange', None, data, writer=None)


    async def global_write(self, message):
        """
        Something like "print" but the config defines, if it forwards to stdin
        or to the networksocket
        """
        for w in self._writers:
            w.write(message.encode('utf-8'))
            w.write(b"\n")

        retvals = await asyncio.gather(*[w.drain() for w in self._writers])

        closed = []
        for r, w in zip(self._writers, retvals):
            if isinstance(r, Exception):
                w.close()
                await w.drain()
                closed.append(w)

        self._writers = [ w for w in self._writers if w not in closed ]


    async def handle_client(self, reader, writer):
        """
        Handler function called whenever an observer connects.
        This is used to connect read and write pipes
        """
        self._writers.append(writer)

        # Run the interaction
        try:
            while True:
                # Await any incoming message
                line = await reader.readline()

                if not line:
                    break

                line = line.decode('utf-8').strip()
                if not line:
                    continue

                lineargs = line.split(' ')
                try:
                    await asyncio.gather(*[callback(self, writer, lineargs)
                                           for callback in self._commands[lineargs[0]]])
                    writer.write(b"\n")
                    await writer.drain()
                except KeyError:
                    writer.write(("ERROR: command not found: %s\n"%lineargs[0])
                                 .encode('utf-8'))
                    await writer.drain()
                    # and hope it was the lineargs problem
        except ConnectionResetError:
            pass
        except ConnectionError as e:
            print("Caught Conn Error", e)
        finally:
            # Upon termination. remove the writer again
            self._writers = [w for w in self._writers if w != writer]

    async def interactionsocket(self):
        """
        Manage keyboard interaction in interactive mode
        """

        server = None

        if self.config['mammutfsd']['interaction'] == "stdin":
            # We try not to have to connectthe stdin reader, if not neccessary
            writer_transport, writer_protocol = await self.loop.connect_write_pipe(
                asyncio.streams.FlowControlMixin, sys.stdout)
            stdout_writer = asyncio.StreamWriter(writer_transport, writer_protocol, None,
                                             self.loop)

            stdin_reader = asyncio.StreamReader()
            reader_protocol = asyncio.StreamReaderProtocol(stdin_reader)
            await self.loop.connect_read_pipe(lambda: reader_protocol, sys.stdin)
            stdin_task = self.loop.create_task(
                self.handle_client(stdin_reader, stdout_writer))

            self._writers.append(stdout_writer)
        else:
            stdin_task = None

        port = self.config['mammutfsd']['port']
        server = await asyncio.start_server(self.handle_client, '127.0.0.1',
                                            int(port), loop=self.loop)

        # This will wait forever - or until a CancelledError is thrown!
        noevent = asyncio.Event(loop=self.loop)
        try:
            await noevent.wait()
        except asyncio.CancelledError:
            pass
        finally:
            if stdin_task:
                stdin_task.cancel()
                try:
                    await stdin_task
                except asyncio.CancelledError:
                    pass

            server.close()

    async def sendall(self, command):
        """
        Send the command to all connected clients
        """
        await asyncio.gather(*[client.write(command) for client in self._clients])

    async def teardown(self):
        """
        The last one to leave pays the bill
        """
        if self._interactionsocket:
            try:
                self._interactionsocket.cancel()
                await self._interactionsocket
            except asyncio.CancelledError:
                pass

        for plugin in self._plugins:
            try:
                getattr(plugin, 'teardown')
            except AttributeError:
                continue

            try:
                await plugin.teardown()
            except asyncio.CancelledError:
                pass

        for client in self._clients:
            try:
                await client.close()
            except asyncio.CancelledError:
                pass

        try:
            self._clientmanagementtask.cancel()
            await self._clientmanagementtask
        except asyncio.CancelledError:
            pass

def main():
    """
    Actually start the mammutfs daemon
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True, help="Configfile to load")
    args = parser.parse_args()

    loop = asyncio.get_event_loop()
    mammutfs = MammutfsDaemon(args.config, loop, **vars(args))

    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        print("Thank you for using the daemon. Time to say goodbye")
        loop.run_until_complete(mammutfs.teardown())

if __name__ == "__main__":
    main()
