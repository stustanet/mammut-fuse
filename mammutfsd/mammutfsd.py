#!/usr/bin/env python3

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

class mammutfsdclient:
    def __init__(self, reader, writer, mfsd):
        self.reader = reader
        self.writer = writer
        self.mfsd = mfsd
        self.read_task = None
        self.plugin_call_task = None

        self._plugin_fileop_queue = asyncio.Queue(loop=self.mfsd.loop)

        self.details = {}

        self._request_pending = False
        self._request_queue = asyncio.Queue(loop=self.mfsd.loop)


    async def run(self):
        self.read_task = self.mfsd.loop.create_task(self.readloop())
        self.plugin_call_task = self.mfsd.loop.create_task(self.plugin_call_loop())

    async def readloop(self):
        # Say hello to the client
        while True:
            line = await self.reader.readline()
            if not line:
                break
            data = json.loads(line.decode('utf-8'))
            if 'op' in data and data['op'] == 'hello':
                self.details = data;
                del self.details['op']
                self.mfsd.log.info("client connect. announced user: %s",
                                   self.details['user'])
                break
            else:
                self.mfsd.log.warning("Invalid client hello received ", line)

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
                    await self._request_queue.put(data)
                else:
                    if 'state' in data:
                        # This is a state message!
                        self.mfsd.log.info("Last command returned: " + str(data))
                    elif 'op' in data and 'module' in data:
                        # Dispatch plugin calls to another coroutine
                        await self._plugin_fileop_queue.put(data)
                    else:
                        self.mfsd.log.warn("Unknown data received: " + str(data))
            except json.JSONDecodeError:
                self.mfsd.log.warn("Invalid data received: " + str(line))


    async def plugin_call_loop(self):
        while True:
            data = await self._plugin_fileop_queue.get()
            await self.mfsd.call_plugin('on_fileop', self, data)


    async def request(self, command):
        self._request_pending = True
        await self.write(command)
        response = await self._request_queue.get()
        self._request_pending = False
        return response

    async def write(self, command):
        try:
            self.writer.write(command.encode('utf-8'))
            await self.writer.drain()
        except ConnectionResetError:
            await self.kill()

    async def kill(self):
        self.mfsd.client_removal_queue.put_nowait(self)
        self.mfsd.log.info("client disconnect")

    async def close(self):
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
        if 'anonym_raid' in self.details:
            return self.details['anonym_raid']
        else:
            response = await self.request("anonym_raid")
            if response['state'] == 'success':
                raid = response['response']
                self.details['anonym_raid'] = raid
                return raid
        return ""

    async def user(self, module):
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

        for k, v in kwargs.items():
            self.config['mammutfsd'][k] = v

        self.log = logging.getLogger('main')
        self._janitortask = None
        self._interactionsocket = None
        self._commands = {}
        self._plugins = []
        self._clients = []
        self.client_removal_queue = asyncio.Queue()

    async def start(self, loop=None):
        """ Start the different parts of the daemon """
        if not loop:
            loop = asyncio.get_event_loop()
        self.loop = loop

        await asyncio.start_unix_server(self.client_connected, loop=loop,
                                        path=self.config['daemon_socket'])

        if self.config["mammutfsd"]["interactive"]:
            self._interactionsocket = loop.create_task(self.interactionsocket())

        self._janitortask = loop.create_task(self.monitorsocket())
        await self._load_plugins()

    def register(self, command, callback):
        try:
            self._commands[command].append(callback)
        except KeyError:
            self._commands[command] = [callback]

    async def _load_plugins(self):
        # TODO Read "mammutfsd_plugins"-list
        configured_plugins = list(self.config["mammutfsd"]["plugins"]) + \
                             [ 'mammutfsd_help' ]
        for pluginname in configured_plugins:
            try:
                module = __import__(pluginname)
                self._plugins.append(await module.init(self.loop, self))
            except AttributeError as exp:
                # The plugin did not have a teardown method
                self.log.info("Attribute Error")
                self.log.exception(exp)
            except ImportError:
                self.log.error("Could not load plugin: %s", pluginname)
                pass

    async def call_plugin(self, function, client, args):
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
        Called whenever a new client connected
        """

        client = mammutfsdclient(reader, writer, self)
        self._clients.append(client)
        await self.call_plugin('on_client', client, {})
        await client.run()

    async def monitorsocket(self):
        while True:
            # An client will insert itself into here if it wants to die
            # some kind of suicide booth
            to_remove = await self.client_removal_queue.get()
            self._clients = [ client for client in self._clients if client != to_remove ]
            await to_remove.close()


    async def interactionsocket(self):
        """
        Manage keyboard interaction in interactive mode
        """

        reader = asyncio.StreamReader()
        reader_protocol = asyncio.StreamReaderProtocol(reader)
        await self.loop.connect_read_pipe(lambda: reader_protocol, sys.stdin)

        while True:
            line = await reader.readline()
            line = line.decode('utf-8').strip()
            if not line:
                continue
            lineargs = line.split(' ')
            try:
                asyncio.gather(*[callback(lineargs)
                                 for callback in self._commands[lineargs[0]]])
            except KeyError:
                self.log.error("command not found: %s", lineargs[0])
                # and hope it was the lineargs problem

    async def sendall(self, command):
        await asyncio.gather(*[client.write(command) for client in self._clients ])

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
    parser.add_argument('--config', help="Configfile to load")
    args = parser.parse_args()

    loop = asyncio.get_event_loop()
    mammutfs = MammutfsDaemon(args.config, **vars(args))
    loop.run_until_complete(mammutfs.start())
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        loop.run_until_complete(mammutfs.teardown())

if __name__ == "__main__":
    main()
