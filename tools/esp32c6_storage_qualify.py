#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_storage_qualify.py - Native C6 storage fixtures over the P4 console.
#
# SPDX-License-Identifier: Apache-2.0
"""Exercise a storage-test image; writes the reserved _m4_ file fixtures."""
import argparse
import json
from pathlib import Path
import time
import zlib
from esp32c6_transport import Console


def main():
    """Record CRUD, cell protection, reset preservation and optional empty-store fill."""
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('port','p4-serial'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--build',required=True,type=int)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--allow-fixture-writes',action='store_true',required=True)
    parser.add_argument('--pause-packets',action='store_true')
    parser.add_argument('--fill-empty-store',action='store_true')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    result={'passed':False,'packet_traffic':not args.pause_packets,'checks':[]}
    console=None
    try:
        console=Console(args.port,args.p4_serial,args.output)
        def command(word,expected=None):
            began=time.monotonic();value=int(console.native(word))
            result['checks'].append({'command':word,'value':value,
                                     'seconds':time.monotonic()-began})
            if expected is not None and value!=expected:
                raise ValueError(word+': '+str(value)+' != '+str(expected))
            return value
        def mode(word):
            reply=console.command('ipc sdio '+word)
            if 'SDMMC result=0 state=2 ' not in reply:raise ValueError(reply)
        if args.pause_packets:
            mode('loader');mode('pause');mode('native')
        command('build',args.build);command('storage',1);command('held',0)
        command('fsmount',1)
        if args.fill_empty_store:
            command('fscount',0)
            capacity=command('fscap')
            if not 1<=capacity<256:raise ValueError('unexpected file capacity')
            for n in range(capacity):
                command('fsfill '+str(n),1)
                if n%32==31:print('created '+str(n+1)+' fixtures',flush=True)
            command('fscount',capacity);command('fsfill '+str(capacity),0)
            command('fserr',2)
            command('fsmount',1);command('fscount',capacity)
            for n in range(capacity):
                command('fsdrop '+str(n),1)
                if n%32==31:print('removed '+str(n+1)+' fixtures',flush=True)
            command('fscount',0)
        for seed in (17,29):
            command('fsput '+str(seed),1);command('fsmount',1)
            command('fslen',4113)
            expected=zlib.crc32(bytes((n*37+seed)&255 for n in range(4113)))
            command('fsget',expected)
        command('cellset 941',1);command('cellget',941);command('cellbad',1)
        mode('native')
        command('storage',1);command('cellget',941);command('fsmount',1)
        command('fsget',expected);command('r 12',0)
        command('fsrm',1);command('fsmount',1)
        result['passed']=True
    except Exception as error:
        result['error']=str(error);raise
    finally:
        if console:
            if args.pause_packets:
                try:console.command('ipc sdio resume')
                except Exception as error:result['resume_error']=str(error)
            console.close()
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: C6 storage fixtures, protection and EN-reset preservation',flush=True)


if __name__=='__main__':main()
