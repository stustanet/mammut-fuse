#!/usr/bin/env python3

import asyncio
import socket


class MammutfsProtocol(asyncio.Protocol):
    def __init__(self, messagebus):
        self.socket = messagebus

    def connection_made(self, transport):
        self.transport = transport
        self.socket.settransport(transport)

    def connection_lost(self, exc):
        self.transport.close()
        self.socket.close()

    def data_received(self, data):
        self.socket._data(data)

    def eof_received(self):
        self.transport.close()
        self.socket.close()

    def error_received(self, exc):
        print("ERROR: ")
        self.transport.close()
        self.socket.close()

class MammutfsSocket:
    _transport = None
    # Write to _last_msg if no argument was pending.
    _last_msg = None
    # If an argument was running, return the next result line to the argument
    _arg_pending = False
    _arg_result = None


    def __init__(self, loop, messagebus):
        self.loop = loop
        self._messagebus = messagebus

    async def connect(self, socketname):
        try:
            print("Creating socket for " + socketname)
            self.socketname = socketname
            self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.socket.connect(socketname)
            self.factory = await self.loop.create_unix_connection(
                lambda: MammutfsProtocol(self),
                path=None, sock=self.socket)
        except Exception as e:
            print("Connection to socket failed: " + str(e))

    def _data(self, recv):
        if self._arg_pending:
            self._arg_result.set_result(recv)
        else:
            self._messagebus.feed_data(recv)

    async def send_command(self, command):
        self._arg_result = self.loop.create_future()
        self._transport.write(str.encode(command.strip()))
        self._arg_pending = True
        await self._arg_result;
        self._arg_pending = False
        return self._arg_result.result()

    def close(self):
        self.socket.close()

    def settransport(self, transport):
        self._transport = transport

def update_anon_map(anon_map_file):
    pass
