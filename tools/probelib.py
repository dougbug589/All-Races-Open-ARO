"""Persistent connection helper for the in-game probe."""
import socket, struct, time
class Probe:
    def __init__(self, port=47777):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=120); self.buf = b""
    def cmd(self, c):
        self.s.sendall((c + "\n").encode())
        while b"\n.\n" not in self.buf and not self.buf.startswith(b".\n"):
            self.buf += self.s.recv(1 << 20)
        if self.buf.startswith(b".\n"): out, self.buf = b"", self.buf[2:]
        else:
            i = self.buf.index(b"\n.\n"); out, self.buf = self.buf[:i + 1], self.buf[i + 3:]
        return out.decode(errors="replace")
    def read(self, addr, n):
        txt = self.cmd(f"r {addr:#x} {n}")
        if "unreadable" in txt: return None
        b = bytearray()
        for line in txt.splitlines():
            b += bytes(int(x, 16) for x in line.split()[1:])
        return bytes(b)
    def floats(self, addr, n):
        b = self.read(addr, n * 4); return None if b is None else struct.unpack(f"<{n}f", b)
    def hits(self, n=100000):
        return [int(l.split()[0], 16) for l in self.cmd(f"list {n}").splitlines() if l.strip()]
