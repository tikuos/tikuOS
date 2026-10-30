#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_cold_image.py - Preserve a C6 recovery image and append a native SRAM boot segment.
#
# SPDX-License-Identifier: Apache-2.0
"""Build a C6 application with native entry and WAKE-selected SDK recovery."""
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
from esp32c6_resident_image import manifest, symbols

BASE=0x4080a000
LIMIT=0x4081a000

@dataclass
class Segment:
    """Initialized bytes and their preserved position in an ESP application."""
    addr: int
    data: bytes
    file_offs: int

@dataclass
class Application:
    """Validated C6 application entry and initialized segments."""
    entrypoint: int
    segments: list

def checked(data):
    """Decode a checked C6 image with an appended digest."""
    if (len(data)<64 or len(data)>0x180000 or data[0]!=0xe9 or
            not 1<=data[1]<=16 or data[23]!=1 or
            struct.unpack_from('<H',data,12)[0]!=13):
        raise ValueError('malformed C6 image header')
    pos=24;segments=[];checksum=0xef
    for _ in range(data[1]):
        if pos+8>len(data):raise ValueError('truncated segment header')
        address,length=struct.unpack_from('<II',data,pos)
        start=pos+8;pos=start+length
        if length%4 or pos>len(data) or address+length>0x100000000:
            raise ValueError('invalid segment extent')
        if 0x42000000<=address<0x43000000 and address%65536!=start%65536:
            raise ValueError('invalid flash segment mapping')
        block=data[start:pos]
        segments.append(Segment(address,block,start))
        for value in block:checksum^=value
    check_at=pos+15-pos%16
    if (check_at+33!=len(data) or data[check_at]!=checksum or
            any(data[pos:check_at]) or
            hashlib.sha256(data[:check_at+1]).digest()!=data[check_at+1:]):
        raise ValueError('C6 image integrity')
    return Application(struct.unpack_from('<I',data,4)[0],segments)

def compose(recovery,native,*,limit=LIMIT,max_native=49152):
    """Append native bytes without relocating recovery flash-mapped segments."""
    image=checked(recovery)
    if not 256<=len(native)<=max_native or len(native)%4:
        raise ValueError('native initialized image bounds')
    if image.entrypoint<0x40800000 or image.entrypoint>=BASE:
        raise ValueError('recovery entry outside preserved SRAM')
    if not any(s.addr<=image.entrypoint<s.addr+len(s.data) for s in image.segments):
        raise ValueError('recovery entry outside initialized segments')
    if len(image.segments)>=16:raise ValueError('no segment slot for native entry')
    if b'tiku-c6-access-1\0' not in recovery[:512]:
        raise ValueError('recovery application identity')
    for segment in image.segments:
        if segment.addr<limit and segment.addr+len(segment.data)>BASE:
            raise ValueError('recovery segment overlaps native arena')
    out=bytearray(recovery[:24]);out[1]+=1
    struct.pack_into('<I',out,4,BASE)
    pos=24;checksum=0xef
    for _ in image.segments:
        address,length=struct.unpack_from('<II',recovery,pos)
        block=recovery[pos+8:pos+8+length]
        if len(block)!=length:raise ValueError('truncated segment')
        out.extend(recovery[pos:pos+8+length]);pos+=8+length
        for value in block:checksum^=value
    out.extend(struct.pack('<II',BASE,len(native)));out.extend(native)
    for value in native:checksum^=value
    out.extend(bytes(15-len(out)%16));out.append(checksum)
    out.extend(hashlib.sha256(out).digest())
    if len(out)>0x180000:raise ValueError('combined image exceeds application slot')
    result=checked(bytes(out))
    if result.entrypoint!=BASE or len(result.segments)!=len(image.segments)+1:
        raise ValueError('combined image entry/segments')
    for old,new in zip(image.segments,result.segments):
        if old.addr!=new.addr or old.data!=new.data or old.file_offs!=new.file_offs:
            raise ValueError('preserved recovery segment changed')
    return bytes(out)

def append_xip(app, payload):
    """Append ignored segment bytes at physical 0x210000 inside application OTA1."""
    original=checked(app)
    if not payload or len(payload)%4 or len(payload)>0xfffd0:
        raise ValueError('C6 XIP payload bounds')
    # The ROM ignores address-zero padding segments. The flash map is linear:
    # OTA1 base 0x190000 + payload offset 0x80000 = physical 0x210000.
    offset=0x80000
    out=bytearray(app[:24]);out[1]+=1
    if out[1]>16:raise ValueError('no segment slots for C6 XIP')
    pos=24;checksum=0xef
    for segment in original.segments:
        size=8+len(segment.data)
        out.extend(app[pos:pos+size]);pos+=size
        for value in segment.data:checksum^=value
    padding=offset-len(out)-8
    if padding<0 or padding%4:raise ValueError('C6 XIP padding bounds')
    out.extend(struct.pack('<II',0,padding+len(payload)));out.extend(bytes(padding))
    out.extend(payload)
    for value in payload:checksum^=value
    out.extend(bytes(15-len(out)%16));out.append(checksum)
    out.extend(hashlib.sha256(out).digest())
    result=checked(bytes(out))
    if result.segments[-1].file_offs+padding!=offset:raise ValueError('C6 XIP physical address')
    if sum(0x42000000<=s.addr<0x43000000 for s in result.segments)>2:
        raise ValueError('C6 bootloader flash-mapped segment limit')
    return bytes(out)

def main():
    """Check the native ELF and recovery geometry before creating an image."""
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('recovery','native','elf','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    elf=args.elf.read_bytes();native=args.native.read_bytes()
    manifest(elf,native,BASE,1)
    _,_,names=symbols(elf)
    if (names.get('c6_cold_entry')!=BASE or
            names.get('__native_stack_top',LIMIT)>LIMIT-4096):
        raise ValueError('native entry or cold-stack reservation')
    if any(symbol in names for symbol in ('app_main','vTaskStartScheduler','xTaskCreate')):
        raise ValueError('SDK runtime linked into native kernel')
    recovery=args.recovery.read_bytes()
    if names.get('__c6_recovery_entry')!=checked(recovery).entrypoint:
        raise ValueError('native recovery target differs from preserved SDK entry')
    data=compose(recovery,native);args.output.write_bytes(data)
    print(json.dumps({'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
        'native_entry':hex(BASE),'recovery_entry':hex(checked(recovery).entrypoint)}))

if __name__=='__main__':main()
