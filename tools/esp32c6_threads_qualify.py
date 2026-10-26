#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_threads_qualify.py - C6 worker context, timer and SDIO soak evidence.
#
# SPDX-License-Identifier: Apache-2.0
"""Check the C6 worker test image through an identified, idle P4 USB console."""
import argparse
import json
from pathlib import Path
import time
from esp32c6_transport import Console
from esp32c6_resident_update import evidence
from esp32c6_sdio_qualify import validate_stream


def snapshot(console):
    """Read context corruption, guard and progress observations without resetting C6."""
    result={str(n):int(console.native('thr '+str(n))) for n in range(10)}
    result['ticks']=int(console.native('ticks'))
    result['selftest']=int(console.native('thrtest'))
    if result['selftest'] or result['3'] or result['4']!=1 or result['5']!=2 or result['8']!=1:
        raise ValueError('worker corruption or unexpected guard/wake count: '+str(result))
    if result['9']<84:raise ValueError('worker startup checks incomplete')
    return result


def progress(before,after):
    """Require each worker and the system tick to advance between snapshots."""
    for key in ('0','1','2','ticks'):
        delta=(after[key]-before[key])&0xffffffff
        if not 0<delta<0x80000000:
            raise ValueError('worker/tick progress stopped or restarted: '+key)


def main():
    """Run startup assertions, register-checking workers and repeated IPC transfers."""
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('port','p4-serial'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--build',required=True,type=int)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--seconds',type=int,default=3600)
    parser.add_argument('--profiles',nargs='+',choices=('image','sensor','slow','corrupt'),
                        default=['image','sensor','slow','corrupt'])
    args=parser.parse_args()
    if not 10<=args.seconds<=7200:parser.error('--seconds must be 10..7200')
    args.output.mkdir(parents=True,exist_ok=False)
    result={'passed':False,'build':args.build,'required_seconds':args.seconds,
            'streams':[],'observations':[]}
    console=Console(args.port,args.p4_serial,args.output)
    with evidence(args.output,result):
        try:
            if console.native('build')!=str(args.build) or console.native('test')!='1':
                raise ValueError('wrong native build or failed boot self-test')
            result['startup']=snapshot(console)
            if console.native('thrsoak')!='0':raise ValueError('soak start refused')
            before=snapshot(console);began=time.monotonic();last=before
            result['observations'].append(dict(before,elapsed_seconds=0))
            index=0
            while time.monotonic()-began<args.seconds:
                remaining=args.seconds-(time.monotonic()-began)
                if remaining>300 or not result['streams']:
                    profile=args.profiles[index%len(args.profiles)];index+=1
                    print('Starting '+profile+' transfer at '+str(round(time.monotonic()-began))+' seconds',flush=True)
                    output=console.command('ipc sdio '+profile,900)
                    stream=validate_stream(output,profile)
                    stream['profile_name']=profile;result['streams'].append(stream)
                else:
                    until=time.monotonic()+min(10,remaining)
                    while time.monotonic()<until:console.pump()
                current=snapshot(console);progress(last,current);last=current
                elapsed=time.monotonic()-began
                result['elapsed_seconds']=elapsed;result['latest']=current
                result['observations'].append(dict(current,elapsed_seconds=elapsed))
                (args.output/'progress.json').write_text(json.dumps(result,indent=2)+'\n')
                print('Workers pass at '+str(round(elapsed))+' seconds; counts '+
                      ','.join(str(current[str(n)]) for n in range(3)),flush=True)
            if console.native('build')!=str(args.build) or console.native('test')!='1':
                raise ValueError('native build or boot self-test changed during soak')
            result['final']=last;result['passed']=True
            result['hour_qualified']=result['elapsed_seconds']>=3600
            print('PASS: '+str(round(result['elapsed_seconds']))+' seconds, '+
                  str(len(result['streams']))+' validated IPC objects',flush=True)
        finally:console.close()


if __name__=='__main__':main()
