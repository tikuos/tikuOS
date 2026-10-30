#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_firmware_image.py - C6 production SRAM and preserved flash layout checks.
#
# SPDX-License-Identifier: Apache-2.0
"""Compose a board-specific C6 application and checked memory budget report."""
import argparse
import hashlib
import json
from pathlib import Path
from esp32c6_resident_image import symbols
from esp32c6_cold_image import BASE,checked,compose,append_xip

END=0x4086e000
BOARD='firebeetle2_esp32p4_c6'
FLASH=[('boot',0,0x9000,'preserve'),('nvs',0x9000,0x4000,'preserve'),
       ('otadata',0xd000,0x2000,'selection'),('phy',0xf000,0x1000,'preserve'),
       ('factory',0x10000,0x180000,'preserve'),('application',0x190000,0x180000,'update'),
       ('resident',0x310000,0x21000,'preserve'),('file_store',0x331000,0xcb000,'unclaimed'),
       ('durable_mirror',0x3fc000,0x4000,'unclaimed')]


def native_layout(elf,native,xip=b''):
    """Require ordered owned SRAM intervals and exact initialized ELF bytes."""
    entry,sections,names=symbols(elf)
    if (entry!=BASE or names.get('c6_cold_entry')!=BASE or
            names.get('__c6_layout_version')!=2 or names.get('__c6_board_id')!=0x50344336 or
            names.get('__native_arena_end')!=END or
            names.get('__native_image_start')!=BASE or
            names.get('__native_image_end')!=BASE+len(native) or
            not 256<=len(native)<=131072 or len(native)%4 or
            not 0<names.get('__c6_firmware_build',0)<=0xffffffff):
        raise ValueError('C6 firmware target, entry or initialized extent')
    shell=names.get('__c6_shell_profile',0)
    if shell not in (0,1):raise ValueError('unknown C6 shell profile')
    if xip:
        if (not shell or names.get('__c6_xip_start')!=0x42210000 or
                names.get('__c6_xip_end')!=0x42210000+len(xip) or
                not 0<len(xip)<=0xfffd0 or len(xip)%4):
            raise ValueError('C6 XIP declaration mismatch')
    elif names.get('__c6_xip_end',0)!=names.get('__c6_xip_start',0):
        raise ValueError('missing C6 XIP payload')
    spans={}
    for name,start,end in [('initialized','__native_image_start','__native_image_end'),
                           ('static','__bss_start','__bss_end'),
                           ('tier','__tier_sram_start','__tier_sram_end'),
                           ('workers','__worker_reserve_start','__worker_reserve_end'),
                           ('radio','__radio_reserve_start','__radio_reserve_end'),
                           ('irq_stack','__native_irq_bottom','__native_irq_top'),
                           ('main_stack','__native_stack_bottom','__native_stack_top')]:
        lo=names.get(start,0);hi=names.get(end,0)
        if not BASE<=lo<hi<=END-4096:raise ValueError('invalid SRAM span: '+name)
        spans[name]={'start':lo,'bytes':hi-lo}
    for name,start,end,ceiling in [('durable','__uninit_start','__uninit_end',8176),
                                   ('retained','__retained_start','__retained_end',8192 if shell else 4096)]:
        if start not in names and end not in names:continue
        lo=names.get(start,0);hi=names.get(end,0)
        if not BASE<=lo<=hi<=END-4096 or hi-lo>ceiling:
            raise ValueError('invalid SRAM span: '+name)
        if hi>lo:spans[name]={'start':lo,'bytes':hi-lo}
    ordered=sorted((x['start'],x['start']+x['bytes']) for x in spans.values())
    if any(a[1]>b[0] for a,b in zip(ordered,ordered[1:])):raise ValueError('SRAM overlap')
    if (spans['workers']['bytes']!=32768 or spans['radio']['bytes']!=65536 or
            spans['irq_stack']['bytes']!=2048 or spans['main_stack']['bytes']!=8192 or
            spans['tier']['bytes']<names.get('__c6_tier_floor',196608) or
            names.get('__c6_tier_floor',0)<65536 or
            names.get('__native_stack_top')!=END-4096):
        raise ValueError('C6 firmware reservation or tier budget')
    if '__c6_recovery_start' in names or '__c6_recovery_end' in names:
        lo=names.get('__c6_recovery_start',0);hi=names.get('__c6_recovery_end',0)
        if hi!=lo and (lo!=names.get('__uninit_start',0)+1024 or hi-lo!=4112 or
                       hi>names.get('__uninit_end',0)):
            raise ValueError('C6 durable recovery-control layout')
    allocated=[]
    for _,kind,flags,address,offset,length,*_ in sections:
        if not flags&2 or not length:continue
        if xip and 0x42210000<=address<0x4230ffd0:
            if (kind!=1 or address+length>0x42210000+len(xip) or
                    elf[offset:offset+length]!=xip[address-0x42210000:address-0x42210000+length]):
                raise ValueError('ELF/XIP payload mismatch')
            allocated.append((address,address+length))
            continue
        if not BASE<=address<=address+length<=END-4096:
            raise ValueError('allocated section outside application SRAM')
        allocated.append((address,address+length))
        if kind==8:
            if address<BASE+len(native):raise ValueError('zero storage overlaps image')
        elif (kind!=1 or address+length>BASE+len(native) or
              elf[offset:offset+length]!=native[address-BASE:address-BASE+length]):
            raise ValueError('ELF/native payload mismatch')
    allocated.sort()
    if any(a[1]>b[0] for a,b in zip(allocated,allocated[1:])):
        raise ValueError('allocated section overlap')
    if any(x in names for x in ('app_main','vTaskStartScheduler','xTaskCreate')):
        raise ValueError('SDK runtime in native kernel')
    spans['cold_entry']={'start':END-4096,'bytes':4096}
    return names,spans


def package(recovery,elf,native,xip=b''):
    """Retain recovery flash mapping and bind the application to its memory report."""
    names,spans=native_layout(elf,native,xip)
    if names.get('__c6_recovery_entry')!=checked(recovery).entrypoint:
        raise ValueError('recovery entry mismatch')
    app=compose(recovery,native,limit=END,max_native=131072)
    if xip:app=append_xip(app,xip)
    metadata={'format':'tiku-c6-firmware','version':2,'board':BOARD,'chip':13,
              'revision_min':2,'revision_max':2,'flash_bytes':0x400000,
              'secure_boot':0,'flash_encrypted':0,'build':names['__c6_firmware_build'],
              'elf_sha256':hashlib.sha256(elf).hexdigest(),
              'recovery_entry':names['__c6_recovery_entry'],
              'tier_floor':names['__c6_tier_floor'],
              'image_bytes':len(app),'image_sha256':hashlib.sha256(app).hexdigest(),
              'native_bytes':len(native),'native_sha256':hashlib.sha256(native).hexdigest(),
              'recovery_sha256':hashlib.sha256(recovery).hexdigest(),
              'memory':spans,'flash':[dict(name=n,start=p,bytes=s,policy=k) for n,p,s,k in FLASH]}
    if xip:
        metadata['xip']={'address':0x42210000,'offset':0x80000,'bytes':len(xip),'sha256':hashlib.sha256(xip).hexdigest()}
    return app,metadata


def validate_package(app,metadata):
    """Refuse changed image identity, board geometry or recovery mapping before I/O."""
    image=checked(app)
    if (metadata.get('format')!='tiku-c6-firmware' or metadata.get('version')!=2 or
            metadata.get('board')!=BOARD or metadata.get('chip')!=13 or
            metadata.get('revision_min')!=2 or metadata.get('revision_max')!=2 or
            metadata.get('flash_bytes')!=0x400000 or
            metadata.get('secure_boot')!=0 or metadata.get('flash_encrypted')!=0 or
            metadata.get('image_bytes')!=len(app) or
            metadata.get('image_sha256')!=hashlib.sha256(app).hexdigest() or
            metadata.get('flash')!=[dict(name=n,start=p,bytes=s,policy=k) for n,p,s,k in FLASH] or
            image.entrypoint!=BASE):raise ValueError('firmware package identity/layout mismatch')
    natives=[s for s in image.segments if s.addr==BASE]
    if len(natives)!=1:raise ValueError('C6 native segment count')
    native=natives[0]
    if 'xip' in metadata:
        ignored=image.segments[-1];info=metadata['xip']
        padding=0x80000-ignored.file_offs
        xip=ignored.data[padding:] if padding>=0 else b''
        if (ignored.addr!=0 or padding<0 or any(ignored.data[:padding]) or
                not 0<len(xip)<=0xfffd0 or len(xip)%4 or
                info!={'address':0x42210000,'offset':0x80000,'bytes':len(xip),
                       'sha256':hashlib.sha256(xip).hexdigest()} or
                image.segments[-2] is not native):
            raise ValueError('C6 XIP image pair mismatch')
    elif image.segments[-1] is not native:raise ValueError('undeclared C6 XIP segment')
    if sum(0x42000000<=s.addr<0x43000000 for s in image.segments)>2:
        raise ValueError('C6 bootloader flash-mapped segment limit')
    if (native.addr!=BASE or metadata.get('native_bytes')!=len(native.data) or
            metadata.get('native_sha256')!=hashlib.sha256(native.data).hexdigest()):
        raise ValueError('firmware/native image pair mismatch')
    return metadata


def validate_pair(app,metadata,elf):
    """Bind the shipped ELF, memory reservations and build identity to the application."""
    validate_package(app,metadata)
    if metadata.get('elf_sha256')!=hashlib.sha256(elf).hexdigest():
        raise ValueError('firmware/ELF identity mismatch')
    image=checked(app)
    native=next(s.data for s in image.segments if s.addr==BASE)
    last=image.segments[-1]
    xip=last.data[0x80000-last.file_offs:] if 'xip' in metadata else b''
    names,spans=native_layout(elf,native,xip)
    if (spans!=metadata.get('memory') or names['__c6_firmware_build']!=metadata.get('build') or
            names['__c6_recovery_entry']!=metadata.get('recovery_entry') or
            names['__c6_tier_floor']!=metadata.get('tier_floor')):
        raise ValueError('firmware memory/build pair mismatch')
    return metadata


def main():
    """Write the checked ESP image and JSON footprint without accessing a board."""
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('recovery','elf','native','output','metadata'):
        parser.add_argument('--'+key,required=True,type=Path)
    parser.add_argument('--xip',type=Path)
    args=parser.parse_args()
    app,meta=package(args.recovery.read_bytes(),args.elf.read_bytes(),args.native.read_bytes(),args.xip.read_bytes() if args.xip else b'')
    validate_pair(app,meta,args.elf.read_bytes())
    args.output.write_bytes(app);args.metadata.write_text(json.dumps(meta,indent=2)+'\n')
    print(json.dumps(meta,indent=2))


if __name__=='__main__':main()
