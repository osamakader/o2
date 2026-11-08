#!/usr/bin/env python3
# A TCP echo server on 127.0.0.1, for tcptest in the guest (qemu's
# user-mode network maps the guest's 10.0.2.2 to the host's loopback).
# Prints the port it listens on, then echoes each connection's bytes
# back until the client closes it, until killed.
import socket, sys, threading

def echo(c):
    with c:
        while True:
            data = c.recv(65536)
            if not data:
                return
            c.sendall(data)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 0))
s.listen()
print(s.getsockname()[1], flush=True)
while True:
    c, _ = s.accept()
    threading.Thread(target=echo, args=(c,), daemon=True).start()
