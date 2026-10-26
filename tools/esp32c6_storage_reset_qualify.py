#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_storage_reset_qualify.py - C6 software-reset storage interruption checks.
#
# SPDX-License-Identifier: Apache-2.0
"""Require C6_STORAGE_TEST and a P4 built with TIKU_SHELL_CMD_DELAY=1."""
import argparse
import json
from pathlib import Path
import zlib
from esp32c6_transport import Console


def hold_host(console,milliseconds):
    """Hold P4 process dispatch while C6 completes its deferred reset and boot."""
    command='delay '+str(milliseconds)
    start=len(console.text);echo=command+'\r\n'
    console.wire.write((command+'\n').encode())
    def done():
        text=console.text[start:];at=text.find(echo)
        return at>=0 and 'tikuOS:/> ' in text[at+len(echo):]
    console.wait(done,10)
    if 'Unknown command' in console.text[start:]:raise ValueError('P4 delay command absent')


def points(count):
    """Select boundaries from the measured uninterrupted operation count."""
    if not 4<=count<=2000:raise ValueError('unexpected flash operation count')
    return sorted({1,2,3,count//2,count-1,count})


def main():
    """Check retained SRAM and old-or-new cell/file recovery between flash operations."""
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('port','p4-serial'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--build',required=True,type=int)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--allow-fixture-writes',action='store_true',required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    result={'passed':False,'interruption':'software reset after completed erase or page program',
            'cell':[],'file':[]}
    console=None
    try:
        console=Console(args.port,args.p4_serial,args.output)
        def native(word,expected=None):
            value=int(console.native(word))
            if expected is not None and value!=expected:raise ValueError(word+': '+str(value))
            return value
        def recover():
            for mode in ('loader','native'):
                text=console.command('ipc sdio '+mode)
                if 'SDMMC result=0 state=2 ' not in text:raise ValueError(text)
            native('storage',1);native('r 12',0)
        hold_host(console,1)
        native('build',args.build);native('storage',1)
        native('warm',1);hold_host(console,2500);recover()
        native('warmok',1);native('warmwhy',3);result['warm_retention']=True
        native('cut 0',1);native('cellset 950',1)
        count=native('flashops');result['cell_operations']=count
        old=native('cellget')
        for index,point in enumerate(points(count)):
            target=1000+index
            native('cut '+str(point),1);native('later '+str(target),1)
            hold_host(console,2500);recover();native('cutok',1);native('warmwhy',3)
            actual=native('cellget')
            if actual not in (old,target):raise ValueError('torn cell')
            result['cell'].append({'point':point,'old':old,'target':target,'actual':actual})
            old=actual;print('cell cut '+str(point)+' passed',flush=True)
        native('fsmount',1);native('fsput 31',1)
        native('cut 0',1);native('fsput 32',1)
        count=native('flashops');result['file_operations']=count
        old=zlib.crc32(bytes((n*37+32)&255 for n in range(4113)))
        file_points=sorted({1,2,3,max(4,count//3),max(4,count//2)})
        for index,point in enumerate(file_points):
            seed=40+index
            expected=zlib.crc32(bytes((n*37+seed)&255 for n in range(4113)))
            native('cut '+str(point),1);native('fsafter '+str(seed),1)
            hold_host(console,2500);recover();native('cutok',1);native('warmwhy',3)
            native('fsmount',1);native('fslen',4113);actual=native('fsget')
            if actual not in (old,expected):raise ValueError('torn file')
            result['file'].append({'point':point,'old_crc':old,'target_crc':expected,'actual_crc':actual})
            old=actual;print('file cut '+str(point)+' passed',flush=True)
        native('cut 9999',1);native('fsafter 77',1)
        hold_host(console,2500);recover();native('cutok',1);native('warmwhy',3)
        native('fsmount',1)
        expected=zlib.crc32(bytes((n*37+77)&255 for n in range(4113)))
        native('fsget',expected);result['after_file_success_crc']=expected
        native('fsrm',1);native('cut 0',1);result['passed']=True
    except Exception as error:
        result['error']=str(error);raise
    finally:
        if console:console.close()
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: C6 warm retention and software-reset commit boundaries',flush=True)


if __name__=='__main__':main()
