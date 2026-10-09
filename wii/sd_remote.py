"""Manage the Wii's SD card from this Mac through wii/sd_tool.c: the helper is
sent to the Homebrew Channel by wiiload and connects back here.

Run: python3 wii/sd_remote.py --wii 192.168.1.224 COMMAND...
  ls sd:/apps                       list a directory
  get sd:/viper/boot.log LOCAL      download a file
  put LOCAL sd:/apps/x/boot.dol     upload a file (parents created)
  putdir LOCALDIR sd:/apps/x        upload a directory tree
  rm sd:/apps/old                   delete a file or a whole directory
  reboot                            restart the console (last command)
Several commands can be chained with ';' as separate arguments, e.g.
  ls sd:/apps \\; rm sd:/apps/viper-bench-l1 \\; ls sd:/apps"""
import argparse
import socket
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hw_remote import local_ip_for, wiiload  # noqa: E402

TOOL = Path(__file__).resolve().parent.parent / 'build/wii/sd_tool/sd_tool.dol'


class Session:
    def __init__(self, conn: socket.socket):
        self.conn = conn
        self.f = conn.makefile('rb')
        self.rebooted = False

    def request(self, line: str, body: bytes = b'') -> bytes:
        self.conn.sendall(line.encode() + b'\n' + body)
        return self.request_reply()

    def request_reply(self) -> bytes:
        line = 'reply'
        head = self.f.readline().decode().strip()
        if head.startswith('ERR'):
            raise RuntimeError(f'{line.split()[0]}: {head}')
        if not head.startswith('OK '):
            raise RuntimeError(f'bad reply {head!r}')
        n = int(head[3:])
        data = self.f.read(n)
        if len(data) != n:
            raise RuntimeError('short reply')
        return data

    def send_frames(self, data: bytes) -> None:
        packed = zlib.compress(data, 6)
        for i in range(0, len(packed), 65536):
            chunk = packed[i:i + 65536]
            self.conn.sendall(struct.pack('>I', len(chunk)) + chunk)
        self.conn.sendall(struct.pack('>I', 0))

    def read_frames(self) -> bytes:
        z, parts = zlib.decompressobj(), []
        while True:
            n = struct.unpack('>I', self.f.read(4))[0]
            if not n:
                break
            chunk = self.f.read(n)
            if len(chunk) != n:
                raise RuntimeError('short frame')
            parts.append(z.decompress(chunk))
        parts.append(z.flush())
        return b''.join(parts)


def run(s: Session, cmd: list[str]) -> None:
    op, args = cmd[0], cmd[1:]
    if op == 'ls':
        print(s.request(f'LIST {args[0]}').decode(), end='')
    elif op == 'get':
        s.request(f'GET {args[0]}')
        Path(args[1]).write_bytes(s.read_frames())
        print(f'got {args[0]} -> {args[1]}')
    elif op == 'put':
        data = Path(args[0]).read_bytes()
        s.conn.sendall(f'PUT {args[1]} {len(data)}\n'.encode())
        s.send_frames(data)
        s.request_reply()
        print(f'put {args[0]} -> {args[1]} ({len(data)} bytes)')
    elif op == 'putdir':
        root = Path(args[0])
        for p in sorted(q for q in root.rglob('*') if q.is_file()):
            run(s, ['put', str(p), f'{args[1].rstrip("/")}/{p.relative_to(root).as_posix()}'])
    elif op == 'rm':
        s.request(f'DEL {args[0]}')
        print(f'deleted {args[0]}')
    elif op == 'reboot':
        s.request('REBOOT')
        s.rebooted = True
        print('rebooting')
    else:
        raise SystemExit(f'unknown command {op}')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--wii', required=True)
    p.add_argument('--port', type=int, default=4301)
    p.add_argument('--tool', type=Path, default=TOOL)
    p.add_argument('command', nargs='+')
    a = p.parse_args()
    cmds, cur = [], []
    for w in a.command:
        if w == ';':
            if cur:
                cmds.append(cur)
            cur = []
        else:
            cur.append(w)
    if cur:
        cmds.append(cur)
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', a.port))
    server.listen(1)
    server.settimeout(120)
    wiiload(a.wii, a.tool, [f'report={local_ip_for(a.wii)}:{a.port}'])
    conn, _ = server.accept()
    conn.settimeout(120)
    s = Session(conn)
    try:
        for c in cmds:
            run(s, c)
    finally:
        try:
            if not s.rebooted:
                s.request('QUIT')
        except (OSError, RuntimeError):
            pass
        conn.close()


if __name__ == '__main__':
    main()
