"""Run a scripted benchmark DOL on a real Wii over the network and collect its
results, with no SD card shuffling.

The Wii must be at the Homebrew Channel menu (it listens for wiiload on TCP
4299). The DOL is sent with the argument report=<this Mac>:<port>; at its
scripted end it sends boot.log and efb.bin back here
(plus ram.bin when the RAM oracle differs), zlib-compressed
(wii/net_report.c) and returns to the Homebrew Channel, ready for the next.

Run: python3 wii/hw_remote.py --wii 192.168.1.224 DOL OUTDIR [--repeat N]"""
import argparse
import os
import re
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

EXPECTED_RAM = os.environ.get('ORACLE_RAM', '40971e3d')   # branch wii-port game config


def local_ip_for(wii: str) -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((wii, 4299))
        return s.getsockname()[0]
    finally:
        s.close()


def wiiload(wii: str, dol: Path, args: list[str], timeout: float = 60) -> None:
    """Homebrew Channel wiiload protocol 0.5: header, zlib data, NUL-separated argv."""
    data = dol.read_bytes()
    packed = zlib.compress(data, 6)
    argv = b''.join(a.encode() + b'\0' for a in [dol.name] + args)
    deadline = time.time() + timeout
    while True:
        try:
            s = socket.create_connection((wii, 4299), timeout=10)
            s.settimeout(120)
            break
        except OSError as e:
            if time.time() > deadline:
                raise SystemExit(f'cannot reach the Homebrew Channel at {wii}:4299 ({e}); is the Wii at the HBC menu?')
            time.sleep(2)
    with s:
        s.sendall(b'HAXX' + bytes([0, 5]) + struct.pack('>HII', len(argv), len(packed), len(data)))
        s.sendall(packed)
        s.sendall(argv)


def receive(server: socket.socket, out: Path, timeout: float) -> list[str]:
    server.settimeout(timeout)
    conn, peer = server.accept()
    conn.settimeout(120)
    got = []
    with conn:
        f = conn.makefile('rb')
        while True:
            line = f.readline()
            if not line or line == b'DONE\n':
                break
            if line.startswith(b'FILE '):   # builds before compression (2026-10-08)
                name = Path(line[5:].strip().decode()).name
                size = struct.unpack('>I', f.read(4))[0]
                (out / name).write_bytes(f.read(size))
                got.append(name)
                continue
            if not line.startswith(b'ZFILE '):
                raise SystemExit(f'bad stream line {line[:40]!r}')
            name = Path(line[6:].strip().decode()).name
            z, parts = zlib.decompressobj(), []
            while True:
                n = struct.unpack('>I', f.read(4))[0]
                if not n:
                    break
                chunk = f.read(n)
                if len(chunk) != n:
                    raise SystemExit(f'{name}: short read')
                parts.append(z.decompress(chunk))
            parts.append(z.flush())
            (out / name).write_bytes(b''.join(parts))
            got.append(name)
    return got


def summary(out: Path) -> str:
    log = out / 'boot.log'
    if not log.exists():
        return 'no boot.log'
    s = log.read_text(errors='replace')
    t = {int(float(g)): int(e) for g, e in re.findall(r'PROFILE guest=([\d.]+) elapsed_us=(\d+)', s)}
    end = re.findall(r'SCRIPTED END result=(\w+).*?ram_fnv32=(\w+)', s)
    res, ram = end[-1] if end else ('NONE', '-')
    drive = (t.get(73, 0) - t.get(70, 0)) / 1e6
    verdict = 'MATCH' if ram == EXPECTED_RAM else 'DIFFERENT'
    return f'{res} ram={ram} {verdict} driving={drive:.3f}s'


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('dol', type=Path)
    p.add_argument('out', type=Path)
    p.add_argument('--wii', required=True)
    p.add_argument('--port', type=int, default=4300)
    p.add_argument('--repeat', type=int, default=1)
    p.add_argument('--timeout', type=float, default=900, help='seconds to wait for one run')
    a = p.parse_args()
    me = local_ip_for(a.wii)
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', a.port))
    server.listen(1)
    for i in range(a.repeat):
        out = a.out / f'run{i + 1}' if a.repeat > 1 else a.out
        out.mkdir(parents=True, exist_ok=True)
        started = time.time()
        wiiload(a.wii, a.dol, [f'report={me}:{a.port}'], timeout=180)
        print(f'[{i + 1}/{a.repeat}] sent {a.dol.name}; waiting for results...', flush=True)
        try:
            files = receive(server, out, a.timeout)
        except socket.timeout:
            print(f'[{i + 1}/{a.repeat}] no results after {a.timeout:.0f}s', flush=True)
            sys.exit(1)
        print(f'[{i + 1}/{a.repeat}] {summary(out)} ({len(files)} files, {time.time() - started:.0f}s) -> {out}', flush=True)
        if i + 1 < a.repeat:
            time.sleep(8)   # let the Homebrew Channel bring its network back up


if __name__ == '__main__':
    main()
