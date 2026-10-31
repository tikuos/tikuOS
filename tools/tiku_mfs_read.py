#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# tiku_mfs_read.py - Independent read-only media image decoder and file export.
#
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import struct
from pathlib import Path


def make_table():
    """Build the reflected CRC32C byte table."""
    table = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0x82F63B78 if value & 1 else 0)
        table.append(value)
    return table


TABLE = make_table()


def crc(data):
    """Compute CRC32C with initial/final inversion."""
    value = 0xFFFFFFFF
    for byte in data:
        value = TABLE[(value ^ byte) & 255] ^ (value >> 8)
    return value ^ 0xFFFFFFFF


def u32(data, offset):
    """Decode one little-endian 32-bit field."""
    return struct.unpack_from('<I', data, offset)[0]


def u64(data, offset):
    """Decode one little-endian 64-bit field."""
    return struct.unpack_from('<Q', data, offset)[0]


class Image:
    """Read a UUID-bound image with independently decoded metadata and data frames."""

    def __init__(self, path, uuid):
        """Recover immutable catalog roots and decode their referenced descriptors."""
        self.path = Path(path)
        self.uuid = bytes.fromhex(uuid)
        if len(self.uuid) != 16:
            raise ValueError('UUID must contain 16 bytes')
        self.sectors = self.path.stat().st_size // 512
        if not 8 * 2048 <= self.sectors <= 1 << 32:
            raise ValueError('unsupported geometry')
        self.bitmap_bytes = (self.sectors // 2048 + 7) // 8
        self.bitmap_sectors = (self.bitmap_bytes + 511) // 512
        self.bank_sectors = 1 + self.bitmap_sectors + 256
        banks = []
        for bank in range(2):
            try:
                banks.append(self.checkpoint(bank))
            except ValueError:
                pass
        if not banks:
            raise ValueError('no valid version-2 checkpoint')
        epoch, sequence, bank, ids, names = max(banks, key=lambda item: item[0])
        for index in range(128):
            lba = bank * self.bank_sectors + 1 + self.bitmap_sectors + 2 * index
            commit = self.sector(lba + 1)
            try:
                self.check(commit, b'MFT2')
            except ValueError:
                break
            if (u64(commit, 24), u64(commit, 32), u64(commit, 40)) != (epoch, sequence + 1, sequence):
                break
            roots = self.sector(lba)
            self.check(roots, b'MFR2')
            if u32(roots, 508) != u32(commit, 48):
                raise ValueError('committed roots checksum mismatch')
            ids, names = self.link(roots, 112), self.link(roots, 144)
            self.roots(ids, names)
            sequence += 1
        descriptors = {}
        for key, node in self.catalog(ids, 1):
            obj = self.sector(u64(node, 120))
            self.check(obj, b'MFO1')
            if u32(obj, 508) != u32(node, 128) or u64(obj, 24) != key:
                raise ValueError('ID/object binding mismatch')
            descriptors[key] = obj
        self.objects, self.pending = {}, {}
        referenced = set()
        for key, node in self.catalog(names, 2):
            for at, target, visible in ((104, self.objects, 1), (112, self.pending, 0)):
                identity = u64(node, at)
                if not identity:
                    continue
                obj = descriptors.get(identity)
                if obj is None or identity in referenced or obj[64:128].split(b'\0', 1)[0] != key or obj[63] != visible:
                    raise ValueError('name/object binding mismatch')
                referenced.add(identity)
                target[key.decode('utf-8', 'surrogateescape')] = obj
        if referenced != set(descriptors):
            raise ValueError('unreferenced catalog object')

    def link(self, page, offset):
        """Decode and bound a subtree reference, height and rank summaries."""
        link = (u64(page, offset), u32(page, offset + 8), u32(page, offset + 12),
                u64(page, offset + 16), u64(page, offset + 24))
        lba, checksum, height, visible, pending = link
        if lba == 0:
            if checksum or height or visible or pending:
                raise ValueError('invalid empty subtree')
        elif not (2048 <= lba < self.sectors and 1 <= height <= 64 and
                  0 < visible + pending <= self.sectors):
            raise ValueError('invalid subtree bounds')
        return link

    def catalog_node(self, link, kind):
        """Validate one AVL page against its bound height and rank counts."""
        lba, checksum, height, visible, pending = link
        node = self.sector(lba)
        self.check(node, b'MFI2')
        if u32(node, 508) != checksum or u32(node, 24) != kind:
            raise ValueError('catalog page binding mismatch')
        left, right = self.link(node, 32), self.link(node, 64)
        own_visible = 1 if kind == 1 else bool(u64(node, 104))
        own_pending = 0 if kind == 1 else bool(u64(node, 112))
        if (height != 1 + max(left[2], right[2]) or abs(left[2] - right[2]) > 1 or
                visible != left[3] + right[3] + own_visible or
                pending != left[4] + right[4] + own_pending):
            raise ValueError('catalog balance or rank mismatch')
        key = u64(node, 96) if kind == 1 else node[144:208].split(b'\0', 1)[0]
        if not key or (kind == 1 and pending) or (kind == 2 and not own_visible and not own_pending):
            raise ValueError('empty catalog key or value')
        return key, node, left, right

    def roots(self, ids, names):
        """Check the two root pages without enumerating files."""
        if ids[4] or ids[3] != names[3] + names[4]:
            raise ValueError('catalog root counts disagree')
        for link, kind in ((ids, 1), (names, 2)):
            if link[0]:
                self.catalog_node(link, kind)

    def catalog(self, root, kind):
        """Enumerate an immutable catalog in key order with bounded traversal state."""
        stack, current, previous = [], root, None
        while current[0] or stack:
            while current[0]:
                key, node, left, right = self.catalog_node(current, kind)
                stack.append((key, node, right))
                if len(stack) > 64:
                    raise ValueError('catalog traversal overflow')
                current = left
            key, node, current = stack.pop()
            if previous is not None and previous >= key:
                raise ValueError('catalog key ordering mismatch')
            previous = key
            yield key, node

    def sector(self, lba, count=1):
        """Read a checked extent with a fresh read-only file descriptor."""
        if lba < 0 or count < 1 or lba + count > self.sectors:
            raise ValueError('out-of-volume reference')
        with self.path.open('rb') as stream:
            stream.seek(lba * 512)
            data = stream.read(count * 512)
        if len(data) != count * 512:
            raise ValueError('short image read')
        return data

    def check(self, page, magic):
        """Check page kind, volume identity and checksum."""
        if page[:4] != magic or page[8:24] != self.uuid or crc(page[:508]) != u32(page, 508):
            raise ValueError('page kind, identity or checksum mismatch')

    def checkpoint(self, bank):
        """Validate a checkpoint bitmap and catalog-root pair."""
        base = bank * self.bank_sectors
        header = self.sector(base)
        self.check(header, b'MFC2')
        if (u64(header, 32), u32(header, 84), u32(header, 88), u32(header, 92)) != (self.sectors, 2, 0, 2048):
            raise ValueError('unsupported format geometry or features')
        bitmap = self.sector(base + 1, self.bitmap_sectors)[:self.bitmap_bytes]
        if crc(bitmap) != u32(header, 76):
            raise ValueError('bitmap checksum mismatch')
        ids, names = self.link(header, 112), self.link(header, 144)
        self.roots(ids, names)
        return u64(header, 40), u64(header, 48), bank, ids, names

    def chunk(self, obj, index):
        """Follow an indexed chunk reference and verify its complete payload."""
        lba, expected = u64(obj, 48), u32(obj, 56)
        height, base, identity = obj[60], 0, u64(obj, 24)
        if not 1 <= height <= 12 or index >> (height * 4):
            raise ValueError('invalid extent height or chunk')
        for level in range(height, 0, -1):
            node = self.sector(lba)
            self.check(node, b'MFN1')
            if u32(node, 508) != expected or (u64(node, 24), u64(node, 32), u32(node, 40)) != (identity, base, level):
                raise ValueError('index binding mismatch')
            child = (index >> (4 * (level - 1))) & 15
            lba, expected = u64(node, 64 + 16 * child), u32(node, 72 + 16 * child)
            base += child << (4 * (level - 1))
            if lba < 2048:
                raise ValueError('reference outside data segments')
        header = self.sector(lba)
        self.check(header, b'MFD1')
        length = u32(header, 40)
        if u32(header, 508) != expected or (u64(header, 24), u64(header, 32)) != (identity, index):
            raise ValueError('frame binding mismatch')
        if length != min(65536, u64(obj, 32) - index * 65536) or not 1 <= length <= 65536:
            raise ValueError('invalid frame length')
        sectors = (length + 511) // 512
        if lba % 2048 + sectors + 2 > 2048:
            raise ValueError('frame crosses segment')
        footer = self.sector(lba + sectors + 1)
        self.check(footer, b'MFE1')
        if u32(footer, 508) != u32(header, 44) or (u64(footer, 24), u64(footer, 32), u32(footer, 40)) != (identity, index, length):
            raise ValueError('footer binding mismatch')
        payload = self.sector(lba + 1, sectors)[:length]
        for start in range(0, length, 4096):
            if crc(payload[start:start + 4096]) != u32(footer, 64 + (start // 4096) * 4):
                raise ValueError('payload checksum mismatch')
        return payload

    def content(self, name):
        """Yield checked chunks and verify the completed object's SHA-256."""
        obj = self.objects[name]
        digest = hashlib.sha256()
        for index in range((u64(obj, 32) + 65535) // 65536):
            data = self.chunk(obj, index)
            digest.update(data)
            yield data
        if obj[62] and digest.digest() != obj[128:160]:
            raise ValueError('object SHA-256 mismatch')


def main():
    """List an image or export one file without modifying image sectors."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image')
    parser.add_argument('uuid')
    parser.add_argument('name', nargs='?')
    parser.add_argument('output', nargs='?')
    args = parser.parse_args()
    image = Image(args.image, args.uuid)
    if args.name is None:
        print(json.dumps([{'name': name, 'id': u64(obj, 24), 'length': u64(obj, 32),
                           'complete': bool(obj[62])} for name, obj in image.objects.items()], indent=2))
    elif args.output is None:
        parser.error('export requires an output path')
    else:
        with open(args.output, 'xb') as output:
            for data in image.content(args.name):
                output.write(data)


if __name__ == '__main__':
    main()
