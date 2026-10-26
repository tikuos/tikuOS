#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_install.py - Checked C6 application installation through P4 SDIO.
#
# SPDX-License-Identifier: Apache-2.0
"""Preserve board flash ownership while installing a validated C6 application."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import zlib
from esp32c6_cold_image import checked
from esp32c6_firmware_image import validate_pair
from esp32c6_transport import Console,Factory

PRESERVED=((0,0x9000),(0xf000,0x1000),(0x10000,0x180000),(0x310000,0xf0000))
PARTITIONS=[(1,2,0x9000,0x4000,'nvs'),(1,0,0xd000,0x2000,'otadata'),
            (1,1,0xf000,0x1000,'phy_init'),(0,16,0x10000,0x180000,'ota_0'),
            (0,17,0x190000,0x180000,'ota_1')]


def validate_live(info,metadata,mac,table,owner):
    """Reject incompatible chip/security, partition geometry and resident ownership."""
    if (info.get('agent')!='tiku-c6-access-1' or info.get('resident')!=1 or
            info.get('chip')!=13 or not metadata['revision_min']<=info.get('revision',-1)<=metadata['revision_max'] or
            info.get('mac')!=mac or info.get('flash_bytes')!=0x400000 or
            info.get('flash_physical_bytes')!=0x400000 or info.get('running_offset')!=0x190000 or
            info.get('next_boot_offset')!=0x190000 or info.get('slot_bytes')!=0x180000 or
            info.get('secure_boot')!=0 or info.get('flash_encrypted')!=0 or
            info.get('force_download_disabled')!=0 or info.get('kept')!=1):
        raise ValueError('C6 live chip, security or application-slot mismatch')
    if len(table)!=4096:raise ValueError('partition table length')
    got=[];at=0
    while at+32<=len(table) and table[at:at+2]==b'\xaa\x50':
        _,kind,subtype,start,size,label,flags=struct.unpack_from('<HBBII16sI',table,at)
        if flags:raise ValueError('partition flags unsupported')
        got.append((kind,subtype,start,size,label.rstrip(b'\0').decode('ascii')));at+=32
    if (got!=PARTITIONS or table[at:at+16]!=b'\xeb\xeb'+b'\xff'*14 or
            table[at+16:at+32]!=hashlib.md5(table[:at]).digest()):
        raise ValueError('foreign or incompatible partition table')
    if (len(owner)!=64 or struct.unpack_from('<4I',owner)!=(0x364e574f,1,0x310000,0x21000) or
            owner[16:22]!=bytes.fromhex(mac.replace(':','')) or
            zlib.crc32(owner[:60])!=struct.unpack_from('<I',owner,60)[0]):
        raise ValueError('foreign or corrupt resident ownership record')


def read_application(console):
    """Bound the resident application's segment walk before reading its complete image."""
    header=console.read(0x190000,24)
    if header[0]!=0xe9 or not 1<=header[1]<=16:raise ValueError('resident image header')
    at=24
    for _ in range(header[1]):
        if at+8>0x180000:raise ValueError('resident segment header bounds')
        raw=console.read(0x190000+at,8);size=struct.unpack_from('<I',raw,4)[0];at+=8+size
        if at>0x180000:raise ValueError('resident segment bounds')
    size=at+15-at%16+33
    if size>0x180000:raise ValueError('resident image digest bounds')
    data=console.read(0x190000,size);checked(data);return data


def preserved(console):
    """Snapshot immutable boot, PHY, factory and resident/foreign tail bytes."""
    return {hex(off):console.rpc(4,off,size).hex() for off,size in PRESERVED}


def save(path,data):
    """Publish a whole host transaction record after flushing its bytes."""
    temporary=path.with_suffix(path.suffix+'.tmp')
    with temporary.open('w') as stream:
        json.dump(data,stream,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
    os.replace(temporary,path)
    directory=os.open(path.parent,os.O_RDONLY|getattr(os,'O_DIRECTORY',0))
    try:os.fsync(directory)
    finally:os.close(directory)


def verify_native(console,metadata):
    """Check the installed core's identity, self-test and admitted SRAM budgets."""
    text=console.command('ipc sdio native')
    if 'SDMMC result=0 state=2 mode=1 ' not in text:raise ValueError('native restart failed')
    checks={'test':1,'build':metadata['build'],'memtest':1,'memsize':409600,
            'tiersize':metadata['memory']['tier']['bytes'],'workers':32768,'radio':65536}
    got={key:console.native(key) for key in checks}
    if any(got[key]!=str(value) for key,value in checks.items()):
        raise ValueError('native memory/identity verification: '+str(got))
    return got


def resume_ready(console,state,metadata,image):
    """Complete an already written image, or select factory for a recorded retry."""
    try:payload=console.bridge()
    except ValueError:return False
    info=json.loads(payload)
    validate_live(info,metadata,state['peer_mac'],console.read(0x8000,4096),
                  console.read(0x310000,64))
    if console.rpc(4,0x190000,len(image))==hashlib.md5(image).digest():
        if preserved(console)!=state['preserved']:raise ValueError('preserved flash changed on resume')
        console.end_loader();state['native']=verify_native(console,metadata)
        state['phase']='complete';state['passed']=True;return True
    console.end_loader(18);return False


def main():
    """Install through the identified P4, or resume a recorded interrupted transaction."""
    parser=argparse.ArgumentParser(description=__doc__)
    for flag in ('port','p4-serial','peer-mac'):parser.add_argument('--'+flag,required=True)
    for flag in ('image','metadata','elf','output'):parser.add_argument('--'+flag,required=True,type=Path)
    operation=parser.add_mutually_exclusive_group()
    operation.add_argument('--resume',action='store_true')
    operation.add_argument('--check-only',action='store_true')
    parser.add_argument('--interrupt',choices=('before-erase','payload','selection'),
                        help='qualification: cancel/reset at the selected update boundary')
    args=parser.parse_args()
    if args.check_only and args.interrupt:parser.error('--interrupt requires installation')
    image=args.image.read_bytes();meta=json.loads(args.metadata.read_text())
    validate_pair(image,meta,args.elf.read_bytes())
    mac=args.peer_mac.lower()
    if len(bytes.fromhex(mac.replace(':','')))!=6:parser.error('peer MAC must contain six bytes')
    if args.resume:
        state=json.loads((args.output/'transaction.json').read_text())
        if (state['peer_mac']!=mac or state['p4_serial']!=args.p4_serial or
                state['image_sha256']!=hashlib.sha256(image).hexdigest() or
                state['previous_sha256']!=hashlib.sha256((args.output/'previous-app.bin').read_bytes()).hexdigest()):
            raise ValueError('resume identity or backup mismatch')
    else:
        args.output.mkdir(parents=True,exist_ok=False)
        state={'passed':False,'peer_mac':mac,'p4_serial':args.p4_serial,
               'image_sha256':hashlib.sha256(image).hexdigest(),'phase':'preflight'}
    if args.resume and 'error' in state:
        state.setdefault('previous_failures',[]).append(state.pop('error'))
        state.pop('cleanup_error',None)
    console=Console(args.port,args.p4_serial,args.output)
    try:
        if args.resume and resume_ready(console,state,meta,image):return
        if not args.resume:
            try:state['previous_build']=console.native('build')
            except ValueError:state['previous_build']=None
            info=json.loads(console.bridge())
            table=console.read(0x8000,4096);owner=console.read(0x310000,64)
            validate_live(info,meta,mac,table,owner)
            state['info']=info;state['preserved']=preserved(console)
            previous=read_application(console)
            with (args.output/'previous-app.bin').open('wb') as backup:
                backup.write(previous);backup.flush();os.fsync(backup.fileno())
            state['previous_sha256']=hashlib.sha256(previous).hexdigest()
            original=checked(previous);prefix=original.segments[-1].file_offs-8
            candidate=checked(image)
            if candidate.segments[-1].file_offs-8!=prefix or image[:prefix]!=previous[:prefix]:
                raise ValueError('update changes preserved recovery segments')
            save(args.output/'transaction.json',state)
            if args.check_only:
                console.end_loader()
                if 'SDMMC result=0 state=2 mode=1 ' not in console.command('ipc sdio native'):
                    raise ValueError('native return after preflight failed')
                state['phase']='checked';state['passed']=True;return
            console.end_loader(18)
        state['phase']='factory';save(args.output/'transaction.json',state)
        factory=Factory(console,mac)
        print('Factory identity verified; installing application',flush=True)
        state['phase']='writing';save(args.output/'transaction.json',state)
        factory.install(image,args.interrupt,
                        lambda n:print('C6 payload '+str(n),flush=True) if n%24576==0 else None)
        state['phase']='attach';save(args.output/'transaction.json',state)
        info=json.loads(console.bridge('attach'))
        if info.get('mac')!=mac or info.get('running_offset')!=0x190000:
            raise ValueError('updated recovery identity mismatch')
        if console.rpc(4,0x190000,len(image))!=hashlib.md5(image).digest():
            raise ValueError('installed application readback mismatch')
        if preserved(console)!=state['preserved']:raise ValueError('preserved flash changed')
        console.rpc(15);console.end_loader()
        state['native']=verify_native(console,meta)
        state['phase']='complete';state['passed']=True
        print('PASS: installed native build '+str(meta['build'])+' with preserved flash',flush=True)
    except InterruptedError as error:
        state['phase']='interrupted';state['interruption']=str(error)
        print('Recorded interruption; resume with the same transaction directory',flush=True)
    except Exception as error:
        state['error']=str(error);raise
    finally:
        if console.active:
            try:
                if console.kind=='factory':console.abort_factory()
                else:console.end_loader()
            except Exception as error:state['cleanup_error']=str(error)
        try:save(args.output/'transaction.json',state)
        finally:console.close()


if __name__=='__main__':main()
