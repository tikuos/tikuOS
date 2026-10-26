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
from esp32c6_cold_image import BASE,checked,compose

END=0x4086e000
BOARD='firebeetle2_esp32p4_c6'
FLASH=[('boot',0,0x9000,'preserve'),('nvs',0x9000,0x4000,'preserve'),
       ('otadata',0xd000,0x2000,'selection'),('phy',0xf000,0x1000,'preserve'),
       ('factory',0x10000,0x180000,'preserve'),('application',0x190000,0x180000,'update'),
       ('resident',0x310000,0x21000,'preserve'),('file_store',0x331000,0xcb000,'unclaimed'),
       ('durable_mirror',0x3fc000,0x4000,'unclaimed')]


def native_layout(elf,native):
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
    ordered=sorted((x['start'],x['start']+x['bytes']) for x in spans.values())
    if any(a[1]>b[0] for a,b in zip(ordered,ordered[1:])):raise ValueError('SRAM overlap')
    if (spans['workers']['bytes']!=32768 or spans['radio']['bytes']!=65536 or
            spans['irq_stack']['bytes']!=2048 or spans['main_stack']['bytes']!=8192 or
            spans['tier']['bytes']<names.get('__c6_tier_floor',196608) or
            names.get('__c6_tier_floor',0)<65536 or
            names.get('__native_stack_top')!=END-4096):
        raise ValueError('C6 firmware reservation or tier budget')
    allocated=[]
    for _,kind,flags,address,offset,length,*_ in sections:
        if not flags&2 or not length:continue
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


def package(recovery,elf,native):
    """Retain recovery flash mapping and bind the application to its memory report."""
    names,spans=native_layout(elf,native)
    if names.get('__c6_recovery_entry')!=checked(recovery).entrypoint:
        raise ValueError('recovery entry mismatch')
    app=compose(recovery,native,limit=END,max_native=131072)
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
    native=image.segments[-1]
    if (native.addr!=BASE or metadata.get('native_bytes')!=len(native.data) or
            metadata.get('native_sha256')!=hashlib.sha256(native.data).hexdigest()):
        raise ValueError('firmware/native image pair mismatch')
    return metadata


def validate_pair(app,metadata,elf):
    """Bind the shipped ELF, memory reservations and build identity to the application."""
    validate_package(app,metadata)
    if metadata.get('elf_sha256')!=hashlib.sha256(elf).hexdigest():
        raise ValueError('firmware/ELF identity mismatch')
    names,spans=native_layout(elf,checked(app).segments[-1].data)
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
    args=parser.parse_args()
    app,meta=package(args.recovery.read_bytes(),args.elf.read_bytes(),args.native.read_bytes())
    validate_pair(app,meta,args.elf.read_bytes())
    args.output.write_bytes(app);args.metadata.write_text(json.dumps(meta,indent=2)+'\n')
    print(json.dumps(meta,indent=2))


if __name__=='__main__':main()
