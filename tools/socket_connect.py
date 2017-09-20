#!/usr/bin/env python3

import asyncio
import socket
import sys

#class MammutfsProtocol(asyncio.DatagramProtocol):
class MammutfsProtocol(asyncio.Protocol):
    def connection_made(self, transport):
        self.transport = transport

    def connection_lost(self, exc):
        print("\rConnection lost")
        self.transport.close()
        loop = asyncio.get_event_loop()
        loop.stop()

    def data_received(self, data):
        print("\r", end='')
        print(data.decode('ascii'))
        print("> ", end='',flush=True)

    def eof_received(self):
        print("\rEOF received");
        self.transport.close()
        loop = asyncio.get_event_loop()
        loop.stop()

    def error_received(self, exc):
        print("ERROR: ")
        self.transport.close()
        loop = asyncio.get_event_loop()
        loop.stop()

def stdin_received(socket):
    data = sys.stdin.readline()
    socket.send(data)
    print("> ", end='', flush=True)

class MammutSocket:

    def __init__(self, loop, server_address):
        self.loop = loop
        self.mammutsocket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        print ('connecting to %s' % server_address)
        try:
            self.mammutsocket.connect(server_address)
        except socket.error as msg:
            print (msg)
            sys.exit(1)

        loop.set_debug(True)
        self.connect = loop.create_unix_connection(MammutfsProtocol,
                                                   path=None,
                                                   sock=self.mammutsocket)
        self.transport, self.protocol = self.loop.run_until_complete(self.connect)

    async def send(self, data):
        self.transport.write(str.encode(data.strip()))

    def close(self):
        self.mammutsocket.close()

if __name__ == "__main__":
    loop = asyncio.get_event_loop()
    socket = MammutSocket(loop, sys.argv[1])
    if len(sys.argv) == 4 and sys.argv[2] == "-c":
        try:
            loop.run_until_complete(socket.send(sys.argv[3]))
            loop.close()
        finally:
            print("Closing socket")
        mammutsocket.close()
    elif len(sys.argv) == 2:
        try:
            print("> ", end='', flush=True)
            loop.add_reader(sys.stdin.fileno(), lambda: stdin_received(socket))
            loop.run_forever()
            loop.close()
        finally:
            print("Closing socket")
            if (socket):
                socket.close()
    else:
        print("Usage: ");
        print(sys.argv[0], " unixsocket [-c command]")
        print()
        print("whereby unixsocket is the socket to connect to, and command is the command to execute.")
        print("if command is empty an interactive shell will be spawned")
