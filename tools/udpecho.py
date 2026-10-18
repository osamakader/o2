#!/usr/bin/env python3
# A UDP echo server on 127.0.0.1, for nettest in the guest (qemu's
# user-mode network maps the guest's 10.0.2.2 to the host's loopback).
# Prints the port it listens on, then echoes until killed.
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 0))
print(s.getsockname()[1], flush=True)
while True:
    data, addr = s.recvfrom(65536)
    s.sendto(data, addr)
