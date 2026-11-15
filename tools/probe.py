#!/usr/bin/env python3
"""Client for the in-game memory probe (DevProbe=1 in OpenRaces.ini). Usage: probe.py <command...>"""
import socket, sys
cmd = " ".join(sys.argv[1:]) or "info"
s = socket.create_connection(("127.0.0.1", 47777), timeout=120)
s.sendall((cmd + "\n").encode())
buf = b""
while not buf.endswith(b"\n.\n") and buf != b".\n":
    chunk = s.recv(65536)
    if not chunk: break
    buf += chunk
sys.stdout.write(buf.decode(errors="replace")[:-2])
