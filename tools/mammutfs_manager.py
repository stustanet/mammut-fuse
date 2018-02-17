#!/usr/bin/env python3

import argparse
import asyncio
import itertools
import os
import pyinotify
import socket
import sys

class MammutfsProtocol(asyncio.Protocol):
    def __init__(self, mammutfs):
        self.mammutfs = mammutfs

#    def connection_made(self, transport):
#        self.transport = transport

#    def connection_lost(self, exc):
#        print("\rConnection lost")
#        self.transport.close()
#        loop = asyncio.get_event_loop()
#        loop.stop()

    def data_received(self, data):
        self.mammutfs.received(data.decode('utf-8'))

#    def eof_received(self):
#        print("\rEOF received");
#        self.transport.close()
#        loop = asyncio.get_event_loop()
#        loop.stop()

#    def error_received(self, exc):
#        print("ERROR: ")
#        self.transport.close()
#        loop = asyncio.get_event_loop()
#        loop.stop()


class AsyncioNotifier(pyinotify.Notifier):
    """
    Notifier subclass that plugs into the asyncio event loop.
    """
    def __init__(self, watch_manager, loop, callback=None,
            default_proc_fun=None, read_freq=0, threshold=0, timeout=None):
        self.loop = loop
        self.handle_read_callback = callback
        pyinotify.Notifier.__init__(self, watch_manager, default_proc_fun, read_freq,
                threshold, timeout)
        loop.add_reader(self._fd, self.handle_read)

    def handle_read(self, *args, **kwargs):
        self.read_events()
        self.process_events()
        if self.handle_read_callback is not None:
            self.handle_read_callback(self)



class Mammutfs:
    def __init__(self, socketname, loop):
        """
        Open up one single socket
        """
        print ('connecting to %s' % socketname)
        self.mammutsocket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.mammutsocket.connect(socketname)
        except socket.error as msg:
            print (msg)
            raise
        self.connect = loop.create_unix_connection(lambda: MammutfsProtocol(self),
                                                   path=None,
                                                   sock=self.mammutsocket)
        self.transport, self.protocol = loop.run_until_complete(self.connect)
        self.pending_data = []

    def received(self, data):
        self.pending_data.append(data)

    def command(self, cmd):
        """
        Send a command to the connected
        """
        self.transport.write(str.encode(cmd.strip()))

    def state(self):
        """
        Receive any data from the socket
        """
        yield self.transport.readline()

    def close(self):
        """
        Close the socket
        """
        self.transport.close()


class Mammutfssocketdir:

    class EventHandler(pyinotify.ProcessEvent):
        def __init__(self, mammutfsdir):
            self.mammutfsdir = mammutfsdir

        def process_IN_CREATE(self, event):
            self.mammutfsdir.add_socket(event.pathname);

    def __init__(self, socketdir, loop, acceptfilter = None, ):
        """
        Create a new socket listener to scan the directory for sockets
        Acceptfilter will be applied, if the socket shall be added to the pool
        """
        self.sockets = []

        # Setup watches
        self.wm = pyinotify.WatchManager()
        self.handler = Mammutfssocketdir.EventHandler(self)
        self.notifier = AsyncioNotifier(self.wm, loop, default_proc_fun=self.handler)

        mask = pyinotify.IN_DELETE | pyinotify.IN_CREATE
        self.wm.add_watch(socketdir, mask, rec=True, auto_add=True)

        # And add all existing sockets
        try:
            for filename in os.listdir(socketdir):
                sockets.append(Mammutfs(socketdir + "/" + filename, loop))
        except:
            raise


    def command(self, cmd):
        """
        Send a command to all connected sockets
        """
        for s in self.sockets:
            s.command(cmd)

    def add_socket(self, name):
        """ Add a socket to the socket pool of this method. """
        self.sockets.append(Mammutfs(name))

    def state(self, cmd):
        """
        Combine the setream of all sockets into one, decoding into dicts
        """
        yield asyncio.gather((s.state for s in self.sockets), loop)


async def send_commands(sockets, dirs, commands):
    if commands:
        for s in itertools.chain(sockets, dirs):
            for c in commands:
                s.command(c)

def stdin_received(targets):
    data = sys.stdin.readline()
    for t in targets:
        t.command(data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Mammutfs management interface.')
    parser.add_argument('target', type=str, nargs="+",
                         help='Socket or socket directory to target the fs to')
    parser.add_argument('--cmd', dest='cmd', action='append_const', const=str,
                         help='send command to the connected socket(s)')
    parser.add_argument('--interactive', dest='interactive', action='store_false',
                        help='keep the interface open as an interactive shell')

    args = parser.parse_args()

    sockets = []
    dirs = []
    loop = asyncio.get_event_loop()
    if isinstance(args.target, list):
        for t in args.target:
            if os.path.isdir(t):
                dirs.append(Mammutfssocketdir(t, loop))
            else:
                sockets.append(Mammutfs(t, loop))
    else:
        if os.path.isdir(args.target):
            dirs.append(Mammutfssocketdir(t, loop))
        else:
            sockets.append(Mammutfs(t, loop))

    loop.run_until_complete(send_commands(sockets, dirs, args.cmd))

    if args.interactive:
        loop.add_reader(sys.stdin.fileno(),
                lambda: stdin_received(itertools.chain(sockets, dirs)))
        loop.run_forever()

    loop.close()
