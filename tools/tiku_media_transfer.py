#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# tiku_media_transfer.py - Checked SD object transfers over USB console or TCP.
# SPDX-License-Identifier: Apache-2.0
"""Upload, resume, list and download native media objects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import sys
import tempfile
import time

HEADER = struct.Struct('<4sBBHIiQI')
HELLO, BEGIN, RESUME, DATA, FINISH, RELEASE, OPEN, READ, CLOSE, LIST, NEXT = range(1, 12)
CHUNK = 1024


def crc32c(data):
    """Return the reflected Castagnoli checksum."""
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


def packet(op, seq, arg=0, data=b'', flags=0):
    """Encode one bounded request."""
    if len(data) > CHUNK:
        raise ValueError('payload exceeds transfer limit')
    head = HEADER.pack(b'TMT1', op, flags, 0, seq, 0, arg, len(data))
    return head + struct.pack('<I', crc32c(head + data)) + data


def decode(raw):
    """Validate one response before exposing its fields."""
    if len(raw) < 32 or len(raw) > CHUNK + 32:
        raise ValueError('invalid response size')
    magic, op, flags, reserved, seq, status, arg, size = HEADER.unpack(raw[:28])
    if (magic != b'TMT1' or flags != 0x80 or reserved or size != len(raw)-32 or
            crc32c(raw[:28]+raw[32:]) != struct.unpack('<I', raw[28:32])[0]):
        raise ValueError('invalid response header/checksum')
    return op, seq, status, arg, raw[32:]


class MediaError(RuntimeError):
    """A native filesystem status returned by the board."""
    def __init__(self, status):
        self.status = status
        super().__init__(f'board media error {status}')


class TCP:
    """Length-prefixed packets on one TCP connection."""
    def __init__(self, address):
        host, port = address.rsplit(':', 1)
        self.wire = socket.create_connection((host, int(port)), timeout=30)
        self.buffer = bytearray()
        self.wire.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        mss = self.wire.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
        self.chunk = min(CHUNK, max(1, mss-36))

    def send(self, raw):
        self.wire.sendall(struct.pack('<I', len(raw)) + raw)

    def receive(self):
        while True:
            if len(self.buffer) >= 4:
                size, = struct.unpack('<I', self.buffer[:4])
                if not 32 <= size <= CHUNK+32:
                    raise ValueError('invalid TCP packet length')
                if len(self.buffer) >= size+4:
                    raw = bytes(self.buffer[4:size+4])
                    del self.buffer[:size+4]
                    return raw
            data = self.wire.recv(8192)
            if not data:
                raise ConnectionError('board disconnected')
            self.buffer.extend(data)

    def close(self):
        self.wire.close()


class USB:
    """Exclusive Espressif USB serial console with media channel demultiplexing."""
    def __init__(self, port):
        import serial
        from serial.tools import list_ports
        from esp_usb_console import require_idle_usb_port, usb_console
        require_idle_usb_port(port, list_ports.comports())
        self.wire = usb_console(serial, port)
        self.wire.write_timeout = 30
        self.wire.open()
        self.frame = bytearray()
        self.started = self.escaped = False
        self.pending = bytearray()

    def command(self, text):
        self.wire.write((text+'\n').encode())
        result = bytearray()
        deadline = time.monotonic()+30
        while time.monotonic() < deadline:
            result.extend(self.wire.read(1024))
            response = result
            if b'media: result=' in response and b'tikuOS:/> ' in response:
                if b'media: result=0\r\n' not in response and b'media: result=0\n' not in response:
                    raise RuntimeError('media command failed; mount the card and check the console')
                return
        raise TimeoutError('media shell command timed out')

    def send(self, raw):
        escaped = raw.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc').replace(b'\x00', b'\xdb\xde')
        self.wire.write(b'\xc0\xf7'+escaped+b'\xc0')
        self.wire.flush()

    def receive(self):
        deadline = time.monotonic()+30
        while time.monotonic() < deadline:
            if not self.pending:
                self.pending.extend(self.wire.read(max(1, min(8192, self.wire.in_waiting))))
            while self.pending:
                byte = self.pending[0]
                del self.pending[0]
                if byte == 0xc0:
                    raw = bytes(self.frame)
                    self.frame.clear()
                    self.started, self.escaped = True, False
                    if raw[:1] == b'\xf7':
                        return raw[1:]
                elif self.started:
                    if self.escaped:
                        if byte not in (0xdc, 0xdd, 0xde):
                            self.started = False
                        else:
                            self.frame.append({0xdc: 0xc0, 0xdd: 0xdb, 0xde: 0}[byte])
                        self.escaped = False
                    elif byte == 0xdb:
                        self.escaped = True
                    else:
                        self.frame.append(byte)
                    if len(self.frame) > CHUNK+33:
                        self.started = False
        raise TimeoutError('USB response timed out')

    def close(self):
        self.wire.close()


class Client:
    """Stop-and-wait requests with exact retransmission after lost replies."""
    def __init__(self, transport, key=b''):
        self.transport, self.sequence = transport, 0
        chunk, info = self.call(HELLO, data=key)
        if chunk != CHUNK or len(info) != 20 or struct.unpack('<I', info[:4])[0] != 65536:
            raise ValueError('unsupported media transfer limits')
        self.uuid = info[4:].hex()

    def call(self, op, arg=0, data=b'', flags=0):
        self.sequence += 1
        raw = packet(op, self.sequence, arg, data, flags)
        for attempt in range(3):
            self.transport.send(raw)
            deadline = time.monotonic()+30
            try:
                while time.monotonic() < deadline:
                    response = self.transport.receive()
                    try:
                        rop, seq, status, value, body = decode(response)
                    except ValueError:
                        continue
                    if seq != self.sequence or rop != op:
                        continue
                    if status:
                        raise MediaError(status)
                    return value, body
            except TimeoutError:
                pass
        raise TimeoutError('board did not acknowledge request; reconnect to resume')


def metadata(raw):
    """Decode the object descriptor returned by OPEN or NEXT."""
    if not 50 <= len(raw) <= 112:
        raise ValueError('invalid object metadata')
    ident, size = struct.unpack('<QQ', raw[:16])
    return dict(id=ident, size=size, sha256=raw[16:48].hex(), complete=bool(raw[48]),
                name=raw[49:].decode('utf-8'))


def remote_name(value):
    raw = value.encode('utf-8')
    if not 1 <= len(raw) <= 63 or any(x < 32 or x == 127 for x in raw):
        raise ValueError('remote name must contain 1..63 UTF-8 bytes without control characters')
    return raw


def put(client, source, name, replace=False, resume=None):
    """Publish a file after the board verifies its expected length and SHA-256."""
    name = remote_name(name)
    if resume is not None and not 1 <= resume < (1 << 64):
        raise ValueError('resume ID must be a positive 64-bit integer')
    with open(source, 'rb') as stream:
        digest = hashlib.sha256()
        length = 0
        while data := stream.read(1024*1024):
            digest.update(data)
            length += len(data)
        digest = digest.digest()
        offset = 0
        if resume is None:
            ident, _ = client.call(BEGIN, length, digest+name, int(replace))
        else:
            ident = resume
            try:
                offset, _ = client.call(RESUME, ident, struct.pack('<Q', length)+digest+name)
            except MediaError as error:
                if error.status != -7:
                    raise
                _, raw = client.call(OPEN, data=name)
                info = metadata(raw)
                client.call(CLOSE)
                if info['size'] != length or info['sha256'] != digest.hex():
                    raise ValueError('upload ID is absent and published file differs') from error
                print('Already published and verified')
                return
        print(f'Upload ID {ident}; offset {offset}; volume {client.uuid}; resume with --resume {ident}', flush=True)
        try:
            if offset > length:
                raise ValueError('resume offset exceeds source')
            stream.seek(offset)
            while offset < length:
                data = stream.read(min(getattr(client.transport, 'chunk', CHUNK),
                                       length-offset, 65536-offset % 65536))
                if not data:
                    raise ValueError('source changed during upload')
                accepted, boundary = client.call(DATA, offset, data)
                if accepted != offset+len(data) or len(boundary) != 8:
                    raise ValueError('invalid upload acknowledgement')
                offset = accepted
            client.call(FINISH, length)
        finally:
            # A confirmed release leaves the last synced prefix available for resume.
            try:
                client.call(RELEASE)
            except (OSError, RuntimeError, ValueError):
                pass
    print(f'Uploaded {length} bytes; SHA-256 {digest.hex()}')


def get(client, name, destination, replace=False):
    """Verify a snapshot download before atomically publishing the local file."""
    path = Path(destination)
    if path.exists() and not replace:
        raise FileExistsError(path)
    _, raw = client.call(OPEN, data=remote_name(name))
    temporary = None
    try:
        info = metadata(raw)
        digest = hashlib.sha256()
        offset = 0
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.'+path.name+'.', delete=False) as stream:
            temporary = Path(stream.name)
            while offset < info['size']:
                requested = min(getattr(client.transport, 'chunk', CHUNK), info['size']-offset)
                at, data = client.call(READ, offset, struct.pack('<I', requested))
                if at != offset or len(data) != requested:
                    raise ValueError('short or misplaced download data')
                stream.write(data)
                digest.update(data)
                offset += len(data)
            stream.flush()
            os.fsync(stream.fileno())
        if digest.hexdigest() != info['sha256']:
            raise ValueError('download SHA-256 mismatch')
        if replace:
            os.replace(temporary, path)
        else:
            os.link(temporary, path)
            temporary.unlink()
        print(f'Downloaded {offset} bytes; SHA-256 {digest.hexdigest()}')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        client.call(CLOSE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--serial', metavar='PORT')
    transport.add_argument('--tcp', metavar='HOST:PORT')
    parser.add_argument('--key-file', type=Path, help='32-byte binary TCP access key')
    commands = parser.add_subparsers(dest='command', required=True)
    upload = commands.add_parser('put')
    upload.add_argument('local')
    upload.add_argument('remote')
    upload.add_argument('--resume', type=int)
    upload.add_argument('--replace', action='store_true')
    download = commands.add_parser('get')
    download.add_argument('remote')
    download.add_argument('local')
    download.add_argument('--replace', action='store_true')
    listing = commands.add_parser('ls')
    listing.add_argument('prefix', nargs='?', default='')
    serve = commands.add_parser('serve', help='enable TCP using the USB console and generate its key')
    serve.add_argument('--port', type=int, default=7331)
    args = parser.parse_args()
    if args.command == 'serve' and (not args.serial or not args.key_file or not 1 <= args.port <= 65535):
        parser.error('serve requires --serial, --key-file and a port in 1..65535')
    if args.tcp and not args.key_file:
        parser.error('--tcp requires --key-file')
    wire = None
    try:
        key = args.key_file.read_bytes() if args.tcp else b''
        if args.tcp and (len(key) != 32 or not any(key)):
            raise ValueError('key file must contain a nonzero 32-byte key')
        wire = USB(args.serial) if args.serial else TCP(args.tcp)
        if args.command == 'serve':
            key = os.urandom(32)
            fd = os.open(args.key_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'wb') as stream:
                stream.write(key)
            wire.command(f'media serve {args.port} {key.hex()}')
            print(f'TCP listener enabled on port {args.port}; key saved to {args.key_file}')
            return 0
        if args.serial:
            wire.command('media usb')
        client = Client(wire, key)
        if args.command == 'put':
            put(client, args.local, args.remote, args.replace, args.resume)
        elif args.command == 'get':
            get(client, args.remote, args.local, args.replace)
        else:
            client.call(LIST, data=args.prefix.encode('utf-8'))
            while True:
                try:
                    _, raw = client.call(NEXT)
                except MediaError as error:
                    if error.status == -7:
                        break
                    raise
                print(json.dumps(metadata(raw), ensure_ascii=True))
        return 0
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        print(f'media transfer: {error}', file=sys.stderr)
        return 1
    finally:
        if wire:
            wire.close()


if __name__ == '__main__':
    sys.exit(main())
