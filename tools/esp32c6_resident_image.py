#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_resident_image.py - validate a native C6 resident image and its manifest.
#
# SPDX-License-Identifier: Apache-2.0
"""Validate initialized bytes, runtime extent and fixed-width loader metadata."""
import argparse
from pathlib import Path
import struct
import zlib


def symbols(data):
    """Read defined global symbols from a bounded little-endian ELF32 table."""
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('requires ELF32 little endian')
    kind, machine, version, entry, _, table, flags = struct.unpack_from('<HHIIIII', data, 16)
    size, count = struct.unpack_from('<HH', data, 46)
    if (kind != 2 or machine != 243 or version != 1 or flags & ~1 or
            size != 40 or not 1 <= count <= 4096 or table+size*count > len(data)):
        raise ValueError('requires RV32 ilp32 executable with a bounded section table')
    sections = [struct.unpack_from('<10I',data,table+40*n) for n in range(count)]
    result = {}
    for section in sections:
        _, kind, _, _, offset, length, link, _, _, stride = section
        if kind != 8 and offset+length > len(data):
            raise ValueError('truncated section')
        if kind != 2:
            continue
        if stride != 16 or length % 16 or link >= count or sections[link][1] != 3:
            raise ValueError('invalid symbol table')
        names = sections[link]
        if names[4]+names[5] > len(data):
            raise ValueError('truncated string table')
        pool = data[names[4]:names[4]+names[5]]
        for at in range(offset,offset+length,16):
            name,value,_,info,_,defined = struct.unpack_from('<IIIBBH',data,at)
            if name >= len(pool) or b'\0' not in pool[name:]:
                raise ValueError('invalid symbol name')
            label = pool[name:pool.index(0,name)].decode('ascii')
            if info >> 4 == 1 and not defined and label:
                raise ValueError('undefined global: '+label)
            if info >> 4 == 1 and defined:
                if label in result:
                    raise ValueError('duplicate symbol')
                result[label] = value
    return entry, sections, result


def manifest(elf, image, base, build):
    """Refuse wrong-base, oversized and mismatched images before loader staging."""
    entry, sections, names = symbols(elf)
    runtime = names.get('__native_stack_top',0)-base
    if (base % 4096 or not 0x40800000 <= base <= 0x40870000 or not build or
            not 0 < build <= 0xffffffff or entry != base or
            names.get('__native_image_start') != base or
            names.get('__native_image_end') != base+len(image) or
            not 256 <= len(image) <= 49152 or len(image) % 4 or
            not len(image) <= runtime <= 65536 or runtime % 16):
        raise ValueError('resident image base, size or runtime mismatch')
    for _,kind,flags,address,offset,length,*_ in sections:
        if not flags & 2 or not length:
            continue
        if address < base or address+length > base+runtime:
            raise ValueError('allocated section outside resident arena')
        if kind == 8:
            if address < base+len(image):
                raise ValueError('initialized image overlaps zero/working storage')
        elif (kind != 1 or address+length > base+len(image) or
                elf[offset:offset+length] != image[address-base:address-base+length]):
            raise ValueError('initialized section differs from binary')
    fields = [0x364b4c54,1,base,len(image),runtime,zlib.crc32(image),build]
    return struct.pack('<8I',*fields,zlib.crc32(struct.pack('<7I',*fields)))


def main():
    """Write the checked loader manifest without opening a board port."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',required=True,type=Path)
    parser.add_argument('--image',required=True,type=Path)
    parser.add_argument('--base',required=True,type=lambda s:int(s,0))
    parser.add_argument('--build',required=True,type=lambda s:int(s,0))
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    result = manifest(args.elf.read_bytes(),args.image.read_bytes(),args.base,args.build)
    args.output.write_bytes(result)
    values = struct.unpack('<8I',result)
    print(f'C6 resident: base={values[2]:08x} image={values[3]} runtime={values[4]} crc={values[5]:08x}')


if __name__ == '__main__':
    main()
