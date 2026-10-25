#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_resident_update.py - native C6 bank update through an owned P4 SDIO bridge.
#
# SPDX-License-Identifier: Apache-2.0
"""Check loader identity and image bounds, preserve the other bank and verify digests."""
import argparse
import json
from pathlib import Path
import struct
import sys
import time
import zlib
from contextlib import contextmanager
from esp32c6_resident_image import manifest

HEADER = struct.Struct('<4sBBH6I')


def acquire_prompt(port, pump, ready, clock=time.monotonic):
    """Acquire the USB shell prompt within ten seconds."""
    until=clock()+10;next_write=clock()
    while not ready():
        now=clock()
        if now>=until:raise TimeoutError('initial shell prompt missing')
        if now>=next_write:
            port.write(b'\n');next_write=now+.25
        pump()


@contextmanager
def evidence(path, observations):
    """Keep a terminal result alongside the console on both success and failure."""
    try:
        yield
    except Exception as error:
        observations['error'] = str(error)
        raise
    finally:
        (path/'result.json').write_text(json.dumps(observations,indent=2)+'\n')


def packet(op, sequence, offset=0, length=0, payload=b''):
    """Encode the preserved resident-loader request with its whole-packet CRC."""
    if not sequence or len(payload) > 480:
        raise ValueError('loader request bounds')
    head = HEADER.pack(b'TKC6',1,op,0,sequence,offset,length,0,len(payload),0)
    data = bytearray(head+payload)
    struct.pack_into('<I',data,28,zlib.crc32(data))
    return bytes(data)


def response(data):
    """Reject mismatched lengths, reserved bits and corruption before decoding payload."""
    if not 32 <= len(data) <= 512:
        raise ValueError('loader response bounds')
    magic, version, op, reserved, seq, off, length, status, size, checksum = HEADER.unpack_from(data)
    checked = bytearray(data);checked[28:32]=bytes(4)
    if (magic != b'TKC6' or version != 1 or reserved or size+32 != len(data) or
            zlib.crc32(checked) != checksum):
        raise ValueError('loader response integrity')
    return dict(op=op,sequence=seq,offset=off,length=length,status=status,payload=data[32:])


def main():
    """Update one explicitly selected board through its validated resident loader."""
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--elf',required=True,type=Path)
    parser.add_argument('--image',required=True,type=Path)
    parser.add_argument('--manifest',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--bank',type=int,choices=(0,1),default=0)
    parser.add_argument('--boot-only',action='store_true',help='verify the selected bank against the image and boot without replacing its image')
    args=parser.parse_args()
    meta=args.manifest.read_bytes()
    if len(meta)!=32:
        raise ValueError('manifest size')
    values=struct.unpack('<8I',meta)
    image=args.image.read_bytes()
    if manifest(args.elf.read_bytes(),image,values[2],values[6]) != meta:
        raise ValueError('manifest differs from checked image')
    args.output.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'TikuBench'))
    from tikubench.core.frames import Splitter,frame
    from esp_usb_console import require_idle_usb_port
    import serial
    from serial.tools import list_ports
    require_idle_usb_port(args.port,list_ports.comports())
    observations={'passed':False}
    with evidence(args.output,observations), serial.Serial(args.port,115200,timeout=.01,write_timeout=1,exclusive=True) as port, (args.output/'console.log').open('w') as log:
        splitter=Splitter(markers=(0xf4,0xf5));text='';incoming=[];sequence=0
        def pump():
            nonlocal text
            splitter.feed(port.read(port.in_waiting or 1))
            got=splitter.take_text().decode(errors='replace')
            text+=got;log.write(got);log.flush()
            for marker,payload in splitter.take_frames():
                if marker==0xf5:
                    decoded=response(payload);incoming.append(decoded)
                    record={k:v for k,v in decoded.items() if k!='payload'}
                    record['payload_hex']=decoded['payload'].hex()
                    log.write(json.dumps(record)+'\n');log.flush()
        def wait(fn,timeout=10):
            until=time.monotonic()+timeout
            while not fn():
                if time.monotonic()>until:raise TimeoutError(text[-2000:])
                pump()
        def rpc(op,offset=0,length=0,payload=b''):
            nonlocal sequence
            sequence+=1
            port.write(frame(0xf5,packet(op,sequence,offset,length,payload)))
            wait(lambda:any(p['sequence']==sequence for p in incoming))
            found=next(p for p in incoming if p['sequence']==sequence)
            incoming.remove(found)
            if (found['op']!=op or found['offset']!=offset or found['length']!=length or found['status']):
                raise ValueError('loader RPC refused: '+str({k:v for k,v in found.items() if k!='payload'}))
            return found['payload']
        def command(command):
            start=len(text);port.write((command+'\n').encode())
            wait(lambda:command+'\r\n' in text[start:])
            end=text.index(command+'\r\n',start)+len(command)+2
            wait(lambda:'tikuOS:/> ' in text[end:])
            return text[end:]
        acquire_prompt(port,pump,lambda:'tikuOS:/> ' in text)
        port.write(b'ipc sdio bridge\n')
        wait(lambda:'C6 loader bridge ready' in text)
        wait(lambda:any(p['sequence']==0 for p in incoming))
        first=next(p for p in incoming if p['sequence']==0)
        if first['op']!=1 or first['status']:
            raise ValueError('initial loader INFO refused')
        info=json.loads(first['payload'])
        if info.get('resident')!=1 or info.get('native_base')!=values[2] or info.get('native_bytes')!=65536:
            raise ValueError('resident identity/arena mismatch')
        observations['loader']=info
        previous=rpc(14,args.bank^1)
        if len(previous)!=40 or struct.unpack_from('<I',previous,32)[0]:
            raise ValueError('other kernel bank is not valid; preserving current bank')
        observations['preserved_bank_manifest']=previous.hex()
        rpc(15);owner=rpc(10);observations['owner']=owner.hex()
        before=[rpc(4,0,0xd000),rpc(4,0x10000,0x180000)]
        if not args.boot_only:
            rpc(11,args.bank,len(image),meta)
            for off in range(0,len(image),384):
                chunk=image[off:off+384];rpc(12,off,len(chunk),chunk)
            rpc(13,args.bank)
        current=rpc(14,args.bank)
        if len(current)!=40 or current[:32]!=meta or struct.unpack_from('<II',current,32)!=(0,values[5]):
            raise ValueError('committed image verification failed')
        other=rpc(14,args.bank^1)
        if other!=previous:raise ValueError('other bank changed')
        after=[rpc(4,0,0xd000),rpc(4,0x10000,0x180000)]
        if before!=after:raise ValueError('preserved boot/factory digests changed')
        observations['preserved_md5']=[v.hex() for v in after]
        rpc(16,args.bank)
        wait(lambda:'C6 loader bridge done' in text)
        wait(lambda:text.rstrip().endswith('tikuOS:/>'))
        until=time.monotonic()+2.5
        while time.monotonic()<until:pump()
        observations['native_info']=command('ipc sdio console info')
        observations['native_test']=command('ipc sdio console test')
        if 'C6 reply: TikuOS C6 resident IPC' not in observations['native_info'] or 'C6 reply: 1' not in observations['native_test']:
            raise ValueError('native boot/qualification response missing')
    observations['image_crc']=values[5];observations['image_bytes']=len(image)
    observations['passed']=True
    (args.output/'result.json').write_text(json.dumps(observations,indent=2)+'\n')
    print(json.dumps(observations,indent=2))


if __name__=='__main__':
    main()
