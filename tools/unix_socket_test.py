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

def stdin_received(transport):
    data = sys.stdin.readline()
    transport.write(str.encode(data.strip()))
    print("> ", end='', flush=True)

server_address = '../socket/johannes'
mammutsocket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
print ('connecting to %s' % server_address)
try:
    mammutsocket.connect(server_address)
except socket.error as msg:
    print (msg)
    sys.exit(1)

loop = asyncio.get_event_loop()
loop.set_debug(True)
connect = loop.create_unix_connection(MammutfsProtocol,
                                      path=None,
                                      sock=mammutsocket)
transport, protocol = loop.run_until_complete(connect)
loop.add_reader(sys.stdin.fileno(), lambda: stdin_received(transport))

try:
    print("> ", end='', flush=True)
    loop.run_forever()
    loop.close()
finally:
    print("Closing socket")
    mammutsocket.close()
