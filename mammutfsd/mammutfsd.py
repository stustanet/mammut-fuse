#!/usr/bin/env python3

import aionotify
import argparse
import asyncio
import io
import libconf
import logging
import os
import sys
import json

from libmammutfs import MammutfsSocket

logging.basicConfig(
    level=logging.DEBUG,
    format='%(name)s: %(message)s',
    stream=sys.stderr,
)
log = logging.getLogger('main')

class Mammutfsd:
    """
    Mammut file system administration daemon.
    """

    _messagebus = asyncio.StreamReader()
    _running = {}

    def __init__(self, cfgfile, argv):
        self._argv = argv

        self.watcher = aionotify.Watcher()
        with io.open(cfgfile) as f:
            self.cfg = libconf.load(f)

        self.parsecfg(self.cfg, argv)

    def getcfg(self, key):
        argopt = getattr(self._argv, key, None)
        if argopt:
            return argopt
        else:
            return self._cfg[key]

    def parsecfg(self, cfgfile, argv):
        self.management = self.getcfg('management')
        self.interactive = argv.interactive

        self.socketdir = self.getcfg('socket_directory')
        self.watcher.watch(alias='sockets', path=self.socketdir,
                flags=aionotify.Flags.CREATE | aionotify.Flags.DELETE |
                aionotify.Flags.MODIFY)
        print("setup watch on " + self.socketdir)

    def setup(self, loop):
        self.loop = loop
        self.task = loop.create_task(self._socketdirobserver())

        if self.interactive:
            self.stdintask = loop.add_reader(sys.stdin.fileno(), self._stdinobserver)
        if self.management:
            self.management_task = loop.create_task(self._managementobserver())

        

    async def teardown(self, loop):
        await self.task
        if self.interactive:
            await self.stdintask
        if self.management:
            await self.management_task

    async def _managementobserver(self):
        while True:
            msg = await self.all_msg()
            print("Change: " + msg['op'] + " on file " + msg['path'])

            if msg['op']== 'RMDIR':
                if self._is_anon_root(msg['path']):
                    print("Lost anon dir - Will reload anon-map")
                    await mammutfs.update_anon_map(self.getcfg('anon_mapping_file'))
                    self.all_command("FORCE-RELOAD")
            elif msg['op'] == 'MKDIR':
                if self._is_anon_root(msg['path']):
                    print("Created anon dir - Will reload anon-map")
                    await mammutfs.update_anon_map()
                    self.all_command("FORCE-RELOAD")

    def _stdinobserver(self):
        #print("stdinobserver running")
        data = sys.stdin.readline()
        self.loop.create_task(self._sendmessage(data))

    async def _sendmessage(self, data):
        try:
            result = await self.all_command(data)
            print(result)
        except Exception as e:
            print(e)

        print ("> ", end='', flush=True)

    async def _socketdirobserver(self):
        print("starting inotify watcher")
        await self.watcher.setup(self.loop)
        ## Add all files in the socketdir as mammutfs
        for filename in os.listdir(self.socketdir):
            await self.add_socket(filename)

        while True:
            try:
                print("Observing socket directory")
                event = await self.watcher.get_event()
                flags = aionotify.Flags.parse(event.flags)
                if  aionotify.Flags.CREATE in flags:
                    await self.add_socket(event.name)
                elif aionotify.Flags.DELETE in flags:
                    await self.remove_socket(event.name)
            except Exception as e:
                print(e)
                import pdb; pdb.set_trace()
                raise

    def _is_anon_root(self, path):
        return True

        rex = r'anonymous/[^/]*/[^/]*$'
        return rex.match(path)

    async def add_socket(self, name):
        try:
            name = self.socketdir + "/" + name
            print("adding socket: " + name)
            mfs = MammutfsSocket(self.loop, self._messagebus)
            await mfs.connect(name);
            self._running[name] = mfs
        except (ConnectionRefusedError, FileNotFoundError) as e:
            print("Connection to socket \"" + name + "\" has failed:")
            print(e)
        except Exception as e:
            print(e)
            import pdb; pdb.set_trace()
            raise

    async def remove_socket(self, name):
        if name in self._running.keys():
            print("removing socket: " + name)
            self._running[name].close()
            del self._running[name]
        else:
            print("Socket was not registered: " + name)

    async def all_command(self, command):
        results,_ = await asyncio.wait([mfs.send_command(command) for mfs in self._running.values()])
        r = []
        for task in filter((lambda e: not e is None), results):
            msg = task.result().decode('ascii')
            try:
                r.append(json.loads(msg))
            except json.decoder.JSONDecodeError:
                r.append(msg)
        return r

    async def all_msg(self):
        msg = await self._messagebus.readline()
        msg = msg.decode('ascii')
        return json.loads(msg)

def main():

    parser = argparse.ArgumentParser()
    parser.add_argument('--socket_directory', help="Directory, where mammutfs stores its sockets")
    parser.add_argument('--interactive', type=bool, default=False, help="Open an interactive shell to communicate with all mammutfs instances at once DEFAULT: False")
    parser.add_argument('--management', type=bool, default=True, help="Disable management features of mammutfs (for example reloading of anon.map) DEFAULT: True")

    args = parser.parse_args()
    mmd = Mammutfsd("./mammutfsd.conf", args)

    loop = asyncio.get_event_loop()
    mmd.setup(loop)
    loop.run_forever()
    mmd.teardown(loop)

if __name__ == "__main__":
    main()
