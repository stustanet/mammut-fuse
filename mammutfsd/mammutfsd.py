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
import redis
import pickle

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
        """
        Load the mammutfs config file
        """

        self.listening = True
        self._argv = argv
        self.watcher = aionotify.Watcher()
        with open(cfgfile) as f:
            self.cfg = libconf.load(f)
        self.parsecfg(self.cfg, argv)
        self.teardown_running = False

    def getcfg(self, key):
        """
        Read a config value from command line or config file
        The command line can overwrite values within the configfile
        """
        argopt = getattr(self._argv, key, None)
        if argopt:
            return argopt
        else:
            return self.cfg[key]

    def parsecfg(self, cfgfile, argv):
        """
        Parse the config file and load them.
        """
        self.management = self.getcfg('management')
        self.interactive = argv.interactive
        self.socketdir = self.getcfg('socket_directory')
        self.redis_server = self.getcfg('redis_server')
        self.redis_table = self.getcfg('redis_table')


    def setup(self, loop):
        """
        Create the daemons and start them
        """

        self.loop = loop
        self.loop.run_until_complete(self._socketdirsetup())
        self.task = self.loop.create_task(self._socketdirobserver())

        if self.interactive:
            print("Enabling Interactive mode")
            self.stdintask = loop.create_task(self._stdinobserver())
        if self.management:
            print("Enable Management mode")
            self.management_task = loop.create_task(self._managementobserver())

        # connect to redis
        print("Starting redis")
        self.redis_handle = redis.Redis(host=self.redis_server)

    async def teardown(self):
        """
        Tear it down properly
        """
        if self.teardown_running:
            return
        self.teardown_running = True
        self.listening = False
        print("starting teardown")

        try:
            self.task.cancel()
            await self.task
        except asyncio.CancelledError:
            pass

        if self.stdintask:
            try:
                self.stdintask.cancel()
                await self.stdintask
            except asyncio.CancelledError:
                pass

        if self.management_task:
            try:
                self.management_task.cancel()
                await self.management_task
            except asyncio.CancelledError:
                pass

    async def _managementobserver(self):
        """
        Manage all the changes that happen on mammut.
        These include: reacting to changes and updating all connected listings.
        Also discover changes and transmit them to redis.
        """
        while self.listening:
            try:
                msg = await self.all_msg()
                print("Change: " + msg['op'] + " on file " + msg['path'])

                # Self management stuff
                if msg['op']== 'RMDIR':
                    if self._is_anon_root(msg['path']):
                        print("Lost anon dir - Will reload anon-map")
                        await self.update_anon_map()
                elif msg['op'] == 'MKDIR':
                    if self._is_anon_root(msg['path']):
                        print("Created anon dir - Will reload anon-map")
                        await self.update_anon_map()

                # Redis interaction
                # filter the mammutfs hints needed in the redis db
                #
                # UNLINK  = remove
                # RENAME  = rename/move
                # CREATE  = new file
                # CHANGE  = file changed
                if msg['op'] in ["UNLINK", "RENAME", "CREATE"]:
                    try:
                        bytestr = pickle.dumps((msg['path'], msg['op']))
                        self.redis_handle.lpush(self.redis_table, bytestr)
                    except Exception as e:
                        print("Failed to push change to redis!", e)
            except KeyError:
                # msg did not contain an op field
                pass
            except asyncio.CancelledError:
                raise
            except Exception as e:
                logging.exception(e)


    async def _stdinobserver(self):
        """
        interactive shell provider - not really usable for multiple masters
        """

        reader = asyncio.StreamReader()
        reader_protocol = asyncio.StreamReaderProtocol(reader)
        await self.loop.connect_read_pipe(lambda: reader_protocol, sys.stdin)

        while True:
            data = await reader.readline()
            print("sending", data)
            await self._sendmessage(data)

    async def _sendmessage(self, data):
        """
        Send a message to all terminals and wait for response
        """
        try:
            result = await self.all_command(data)
            print(result)
        except Exception as e:
            logging.exception(e)
        print ("> ", end='', flush=True)

    async def _socketdirsetup(self):
        """
        Be notified, whenever something within the socket directory changes
        (uses inotify watch)
        """
        self.watcher.watch(alias='sockets', path=self.socketdir,
                flags=aionotify.Flags.CREATE | aionotify.Flags.DELETE |
                aionotify.Flags.MODIFY)
        
        print("setup watch on " + self.socketdir)
        try:
            await self.watcher.setup(self.loop)
        except OSError as e:
            logging.exception(e)
            raise

        ## Add all files in the socketdir as mammutfs
        for filename in os.listdir(self.socketdir):
            try:
                await self.add_socket(filename)
            except OSError as e:
                logging.exception(e)
                return

    async def _socketdirobserver(self):
        while self.listening:
            print("Observing socket directory")
            event = await self.watcher.get_event()
            flags = aionotify.Flags.parse(event.flags)
            if  aionotify.Flags.CREATE in flags:
                await self.add_socket(event.name)
            elif aionotify.Flags.DELETE in flags:
                await self.remove_socket(event.name)

    def _is_anon_root(self, path):
        """
        Detect if a path is an anonymous folder, and it was added as a new
        folder in root. Should match to a regex
        """
        return True

        rex = r'anonymous/[^/]*/[^/]*$'
        return rex.match(path)

    async def add_socket(self, name):
        """
        When a new socket appears, it will be added into the pool of known 
        sockets
        """
        try:
            name = self.socketdir + "/" + name
            #print("adding socket: " + name)
            mfs = MammutfsSocket(self.loop, self._messagebus)
            await mfs.connect(name);
            self._running[name] = mfs
        except (ConnectionRefusedError, FileNotFoundError) as e:
            print("Connection to socket \"" + name + "\" has failed:")
            print(e)
        except Exception as e:
            print(e)
            raise
    

    async def remove_socket(self, name):
        """
        remove the socket from the pool of known sockets
        """
        if name in self._running.keys():
            print("removing socket: " + name)
            del self._running[name]
        else:
            print("Socket was not registered: " + name)

    async def all_command(self, command):
        """
        Send a command to all sockets in the pool of known socket
        """
        r = []
        pending = [mfs.send_command(command) for mfs in self._running.values()]
        try:
            results,_ = await asyncio.wait(pending)
            for task in filter((lambda e: not e is None), results):
                try:
                    msg = await task

                    msg = msg.decode('ascii')
                    try:
                        r.append(json.loads(msg))
                    except json.decoder.JSONDecodeError:
                        r.append({'state':'error'})
                except Exception as e:
                    logging.exception(e)
        except Exception as e:
            logging.exception(e)

        return r

    async def all_msg(self):
        """
        Receive any message from any client
        """
        print("waiting")
        msg = await self._messagebus.readline()
        msg = msg.decode('ascii').strip()
        if not msg:
            return {}

        print("data da", msg)
        try:
            return json.loads(msg)
        except json.JSONDecodeError as e:
            logging.exception(e)
            return {}
    
    async def update_anon_map(self):
        # TODO: Update the map...
        await self.all_command("FORCE-RELOAD")


def main():
    """
    Start the magic
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('--socket_directory',
            help="Directory, where mammutfs stores its sockets")
    parser.add_argument('--interactive', 
            type=bool, default=False, 
            help="Open an interactive shell to communicate with all mammutfs instances at once DEFAULT: False")
    parser.add_argument('--management', 
            type=bool, default=True, 
            help="Disable management features of mammutfs (for example reloading of anon.map) DEFAULT: True")

    args = parser.parse_args()
    mmd = Mammutfsd("./mammutfsd.conf", args)

    loop = asyncio.get_event_loop()
    loop.set_debug(True)
    mmd.setup(loop)
    try:
        loop.run_forever()
    finally:
        try:
            loop.run_until_complete(mmd.teardown())
        except Exception as e:
            raise


if __name__ == "__main__":
    main()
