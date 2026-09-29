#!/usr/bin/env python3
"""Reference oracle: drive MAME (env MAME_SET, default thrild2) through its gdbstub debugger.

Starts MAME headless with -debugger gdbstub, connects with a minimal GDB remote
protocol client and exposes break/continue/register/memory primitives, so the
recompiled build can be compared against MAME at the same guest PCs.

usage (library):  o = Oracle(rompath); o.bp(0x2000c); o.cont(); print(o.regs()); o.mem(0x2320, 16)
usage (cli):      mame_oracle.py ROMPATH ADDR [ADDR...]   -> prints regs at the first hits
"""
import os
import socket
import subprocess
import sys
import time

# MAME's PPC gdbstub register order: r0-r31, f0-f31 (64-bit), pc, msr, cr, lr, ctr, xer
GPR, FPR = 32, 32


class Oracle:
    def __init__(self, rompath, port=23946, extra=()):
        self.port = port
        workdir = os.path.dirname(os.path.abspath(rompath))
        self.proc = subprocess.Popen(
            ['mame', os.environ.get('MAME_SET', 'thrild2'), '-rompath', rompath, '-video', 'none', '-sound', 'none', '-nothrottle',
             '-debug', '-debugger', 'gdbstub', '-debugger_port', str(port), '-nvram_directory',
             os.path.join(workdir, 'nv'), '-cfg_directory', os.path.join(workdir, 'cfg'), *extra],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=workdir)
        for _ in range(100):
            try:
                self.sock = socket.create_connection(('127.0.0.1', port))
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise RuntimeError('cannot connect to MAME gdbstub')
        self.sock.settimeout(600)
        self.buf = b''
        self.cmd('?')

    # --- protocol
    def _send(self, payload):
        cs = sum(payload.encode()) & 0xff
        self.sock.sendall(f'${payload}#{cs:02x}'.encode())

    def _recv(self):
        while True:
            while b'#' not in self.buf or len(self.buf) < self.buf.index(b'#') + 3:
                self.buf += self.sock.recv(65536)
            if self.buf[:1] in (b'+', b'-'):
                self.buf = self.buf[1:]
                continue
            s = self.buf.index(b'$')
            e = self.buf.index(b'#', s)
            pkt = self.buf[s + 1:e].decode()
            self.buf = self.buf[e + 3:]
            self.sock.sendall(b'+')
            return pkt

    def cmd(self, payload):
        self._send(payload)
        return self._recv()

    # --- primitives
    def bp(self, addr):
        return self.cmd(f'Z0,{addr:x},4')

    def unbp(self, addr):
        return self.cmd(f'z0,{addr:x},4')

    def cont(self):
        return self.cmd('c')

    def step(self):
        return self.cmd('s')

    def regs(self):
        raw = bytes.fromhex(self.cmd('g'))
        r = {}
        off = 0
        for i in range(GPR):
            r[f'r{i}'] = int.from_bytes(raw[off:off + 4], 'big'); off += 4
        for i in range(FPR):
            r[f'f{i}'] = raw[off:off + 8].hex(); off += 8
        for name in ('pc', 'msr', 'cr', 'lr', 'ctr', 'xer'):
            if off + 4 <= len(raw):
                r[name] = int.from_bytes(raw[off:off + 4], 'big'); off += 4
        return r

    def mem(self, addr, n):
        return bytes.fromhex(self.cmd(f'm{addr:x},{n:x}'))

    def close(self):
        try:
            self._send('k')
        except OSError:
            pass
        self.proc.kill()


def fmt_regs(r):
    lines = []
    for i in range(0, 32, 8):
        lines.append(' '.join(f"r{i + k:<2}={r[f'r{i + k}']:08x}" for k in range(8)))
    lines.append(' '.join(f"{k}={r[k]:08x}" for k in ('pc', 'msr', 'cr', 'lr', 'ctr', 'xer') if k in r))
    return '\n'.join(lines)


if __name__ == '__main__':
    o = Oracle(sys.argv[1])
    try:
        for a in sys.argv[2:]:
            o.bp(int(a, 16))
        stop = o.cont()
        print('stop:', stop)
        print(fmt_regs(o.regs()))
    finally:
        o.close()
