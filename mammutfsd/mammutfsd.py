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
import libconf

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

    def __init__(self, reader, writer, mfsd):
        self.reader = reader
        self.writer = writer
        self.mfsd = mfsd
        self.read_task = None
        self.plugin_call_task = None
        self.details = {}

        self._plugin_fileop_queue = asyncio.Queue(loop=self.mfsd.loop)

        self._request_pending = False
        self._request_queue = asyncio.Queue(loop=self.mfsd.loop)


    async def run(self):
        """
        Start the shiat
        """
        self.read_task = self.mfsd.loop.create_task(self.readloop())
        self.plugin_call_task = self.mfsd.loop.create_task(self.plugin_call_loop())

    async def readloop(self):
        """
        Wait for incoming data, decode it and forward it to the plugins and
        a possible interactive user
        """
        # Hello-loopt
        while True:
            line = await self.reader.readline()
            if not line:
                break
            try:
                data = json.loads(line.decode('utf-8'))
                if 'op' in data and data['op'] == 'hello':
                    self.details = data
                    del self.details['op']
                    self.mfsd.log.info("fs connect. announced user: %s",
                                       self.details['user'])
                    break
                else:
                    self.mfsd.log.warning("Invalid client hello received ", line)
            except json.JSONDecodeError:
                self.mfsd.log.warn("Hello: invalid data received: " + str(line))

        # Working loop
        while True:
            line = await self.reader.readline()
            if not line:
                # We should die - this could be tricky
                # 1. This loop must die   < done here by returning
                # 2. This task mus be removed from the running clients
                # 2. This task must die   < done here by returning
                # 3. This task mus be read
                await self.kill()
                return
            try:
                data = json.loads(line.decode('utf-8'))
                if self._request_pending:
                    # A request can intercept the next flying message
                    # this might actually lead to races, when a file is
                    # changed at the same time
                    await self._request_queue.put(data)
                else:
                    if 'state' in data:
                        # This is a state message!
                        await self.mfsd.write("result: " + str(data) + "\n")
                    elif 'op' in data and 'module' in data:
                        await self.mfsd.write("fileop: " + str(data) + "\n")
                        # Dispatch plugin calls to another coroutine
                        await self._plugin_fileop_queue.put(data)
                    else:
                        self.mfsd.log.warn("Unknown data received: " + str(data))
            except json.JSONDecodeError:
                self.mfsd.log.warn("Invalid data received: " + str(line))

    async def plugin_call_loop(self):
        """
        Plugin calls should not be run in the scope if the readloop,
        because plugins would not be able to send commands to a mammutfs, since
        the recieval loop is blocked.
        Therefore this loops only purpose is to call plugins
        """
        while True:
            data = await self._plugin_fileop_queue.get()
            await self.mfsd.call_plugin('on_fileop', self, data)

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
            await self.kill()

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

    async def kill(self):
        """
        Send this client to the graveyard and enshure that it will not receive
        commands
        """
        self.mfsd.client_removal_queue.put_nowait(self)
        self.mfsd.log.info("fs client disconnect")


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

    def __init__(self, configfile, **kwargs):
        with open(configfile) as cfgf:
            self.config = libconf.load(cfgf)

        for key, value in kwargs.items():
            self.config['mammutfsd'][key] = value

        self.log = logging.getLogger('main')
        self._janitortask = None
        self._interactionsocket = None
        self._commands = {}
        self._plugins = []
        self._clients = []
        self.client_removal_queue = None

        self.loop = None
        self.connect_event = None
        self.reader = None
        self.writer = None
        self.stdin_reader = None
        self.stdout_writer = None

    async def start(self, loop=None):
        """ Start the different parts of the daemon """
        if not loop:
            loop = asyncio.get_event_loop()
        self.loop = loop

        self.client_removal_queue = asyncio.Queue(loop=self.loop)

        self.connect_event = asyncio.Event(loop=self.loop)

        await asyncio.start_unix_server(self.client_connected, loop=self.loop,
                                        path=self.config['daemon_socket'])

        # now we have to change the sockets permissions to 0777 - so mammutfs
        # can actually connect
        os.chmod(self.config['daemon_socket'], 0o777)

        if self.config["mammutfsd"]["interactive"]:
            self._interactionsocket = loop.create_task(self.interactionsocket())

        self._janitortask = loop.create_task(self.monitorsocket())
        await self._load_plugins()

    async def monitorsocket(self):
        """
        An client will insert itself into here if it wants to die as some kind
        of suicide booth
        """
        while True:
            to_remove = await self.client_removal_queue.get()
            self._clients = [client for client in self._clients if client != to_remove]
            await to_remove.close()

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
        for pluginname in cfg_plugins + ['mammutfsd_help']:
            try:
                module = __import__(pluginname)
                self._plugins.append(await module.init(self.loop, self))
            except AttributeError as exp:
                # The plugin did not have a teardown method
                self.log.info("Attribute Error")
                self.log.exception(exp)
            except ImportError:
                self.log.error("Could not load plugin: %s", pluginname)

    async def call_plugin(self, function, client, args):
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
            if asyncio.iscoroutinefunction(func):
                await func(client, args)
            else:
                func(client, args)

    async def client_connected(self, reader, writer):
        """
        Called whenever a new mammutfs connected
        """
        client = MammutfsdClient(reader, writer, self)
        self._clients.append(client)
        await self.call_plugin('on_client', client, {})
        await client.run()


    async def write(self, message):
        """
        Something like "print" but the config defines, if it forwards to stdin
        or to the networksocket
        """
        if not self.writer:
            print(message, end='')
        else:
            try:
                self.writer.write(message.encode('utf-8'))
                await self.writer.drain()
            except IOError:
                self.writer = self.stdout_writer

    async def wait_for_observer(self, reader, writer):
        """
        Handler function called whenever an observer connects.
        This is used to connect read and write pipes
        """
        if self.writer:
            await self.write("Your connection has been overwritten. "
                             "You will not receive any more data\n")

        print("mammutfsd: interaction client connect")
        self.reader = reader
        self.writer = writer
        self.connect_event.set()

    async def interactionsocket(self):
        """
        Manage keyboard interaction in interactive mode
        """

        try:
            _ = self.config['mammutfsd']['interaction']
        except KeyError:
            self.config['mammutfsd']['interaction'] = 'stdin'


        writer_transport, writer_protocol = await self.loop.connect_write_pipe(
            asyncio.streams.FlowControlMixin, sys.stdout)
        self.stdout_writer = asyncio.StreamWriter(writer_transport, writer_protocol, None,
                                                  self.loop)
        server = None

        if self.config['mammutfsd']['interaction'] == 'stdin':
            # We try not to have to connectthe stdin reader, if not neccessary
            self.stdin_reader = asyncio.StreamReader()
            reader_protocol = asyncio.StreamReaderProtocol(self.reader)
            await self.loop.connect_read_pipe(lambda: reader_protocol, sys.stdin)

            self.reader = self.stdin_reader
            self.writer = self.stdout_writer
        elif self.config['mammutfsd']['interaction'] == 'net':
            port = self.config['mammutfsd']['port']
            server = await asyncio.start_server(self.wait_for_observer, '127.0.0.1',
                                                int(port), loop=self.loop)
            await self.connect_event.wait()
            self.connect_event.clear()

        try:
            while True:
                if not self.reader:
                    # The reader is none if it disconnected previously
                    print("mammutfsd: interaction client disconnect")
                    await self.connect_event.wait()
                    self.connect_event.clear()

                line = await self.reader.readline()
                if not line:
                    self.writer = self.stdout_writer
                    self.reader = None
                    continue
                line = line.decode('utf-8').strip()
                if not line:
                    await self.write("") # We send an empty frame, to stop listeness
                    continue
                lineargs = line.split(' ')
                try:
                    asyncio.gather(*[callback(lineargs)
                                     for callback in self._commands[lineargs[0]]])
                except KeyError:
                    await self.write("ERROR: command not found: %s\n"%lineargs[0])
                    # and hope it was the lineargs problem
        finally:
            if server:
                server.close()
                await server.wait_closed()

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
            self._janitortask.cancel()
            await self._janitortask
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
    mammutfs = MammutfsDaemon(args.config, **vars(args))
    loop.run_until_complete(mammutfs.start())
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        print("Thank you for using the daemon. Time to say goodbye")
        loop.run_until_complete(mammutfs.teardown())

if __name__ == "__main__":
    main()
