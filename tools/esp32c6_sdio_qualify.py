#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_sdio_qualify.py - board proof of bounded native SDIO object streams.
#
# SPDX-License-Identifier: Apache-2.0
"""Check native SDIO clock, payloads, control deadlines, backing and recovery."""
import argparse
import json
from pathlib import Path
import re
import sys
import time
from esp32c6_resident_update import evidence
from esp_usb_console import require_idle_usb_port,usb_console


sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'TikuBench'))
from tikubench.core.ipc_sdio import expected_crc,validate_stream,validate_recovery


def command_complete(text,value):
    """Accept a completed SDMMC reply and prompt when command echo is absent."""
    if not value:return 'tikuOS:/> ' in text
    marker=text.rfind('SDMMC result=')
    return marker>=0 and 'tikuOS:/> ' in text[marker:]


def main():
    """Run selected profiles on an explicitly named, idle P4 USB console."""
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--profiles',nargs='+',choices=('raw','image','sensor','slow','corrupt'),default=['image','sensor','slow','corrupt'])
    parser.add_argument('--recovery',action='store_true')
    parser.add_argument('--cold-boot',action='store_true')
    parser.add_argument('--warm-resets',type=int,default=0)
    parser.add_argument('--soak',type=int,default=0,metavar='SECONDS')
    args=parser.parse_args()
    if (not 0<=args.warm_resets<=100 or not 0<=args.soak<=3600 or
            (args.warm_resets and not args.cold_boot)):
        parser.error('warm resets require --cold-boot; counts 0..100, soak 0..3600')
    args.output.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'TikuBench'))
    from tikubench.core.frames import Splitter
    import serial
    from serial.tools import list_ports
    require_idle_usb_port(args.port,list_ports.comports())
    observations={'passed':False,'profiles':{}}
    wire=usb_console(serial,args.port)
    with evidence(args.output,observations),wire,(args.output/'console.log').open('w') as log:
        splitter=Splitter(markers=(0xf4,0xf5));text=''
        def pump():
            nonlocal text
            splitter.feed(wire.read(wire.in_waiting or 1))
            got=splitter.take_text().decode(errors='replace');text+=got;log.write(got);log.flush()
        def command(value,timeout=900):
            start=len(text);wire.write((value+'\n').encode());until=time.monotonic()+timeout
            while True:
                pump();part=text[start:]
                if command_complete(part,value):return part
                if time.monotonic()>until:raise TimeoutError(part[-2000:])
        def native(value):
            result=command('ipc sdio console "'+value+'"',15)
            found=re.search(r'C6 reply: ([^\r\n]+)',result)
            if not found or 'SDMMC result=0 ' not in result:raise ValueError(result)
            return found[1]
        def cycles():
            for _ in range(4):
                high=int(native('workhi'));low=int(native('worklo'))
                if high==int(native('workhi')):return (high<<32)|low
            raise ValueError('cycle snapshot did not converge')
        command('')
        observations['info']=native('info');observations['test']=native('test')
        if observations['test']!='1':raise ValueError('C6 native self-test failed')
        observations['clock_khz']=[int(native('buskhz')) for _ in range(5)]
        if any(not 9500<=n<=10500 for n in observations['clock_khz']):raise ValueError('SDIO clock outside 10 MHz profile')
        observations['cpu_mhz']=int(native('r 7'))
        boot_command='ipc sdio native' if args.cold_boot else 'ipc sdio run 0'
        if args.cold_boot:
            if int(native('r 59'))!=0x434f4c44:raise ValueError('cold-entry report missing')
            observations['reset_reason']=int(native('r 58'))
            observations['warm_resets']=[]
            for index in range(args.warm_resets):
                began=time.monotonic();result=command(boot_command,15)
                if 'SDMMC result=0 state=2 mode=1 ' not in result or native('test')!='1':
                    raise ValueError('native warm reset failed')
                if int(native('r 59'))!=0x434f4c44:raise ValueError('reset used resident entry')
                observations['warm_resets'].append(time.monotonic()-began)
                print('native warm reset '+str(index+1)+' passed',flush=True)
        if args.soak:
            before=cycles();first_tick=int(native('ticks'));began=time.monotonic()
            while time.monotonic()-began<args.soak:
                time.sleep(max(0,min(5,args.soak-(time.monotonic()-began))))
                if native('test')!='1' or int(native('r 12')):
                    raise ValueError('native soak self-test/IRQ error')
            elapsed=time.monotonic()-began;last_tick=int(native('ticks'));after=cycles()
            if not last_tick>first_tick or not after>before:raise ValueError('native counters stopped')
            observations['soak']={'seconds':elapsed,'ticks':last_tick-first_tick,
                                  'scheduler_cycles':after-before}
        for profile in args.profiles:
            before=cycles();output=command('ipc sdio '+('stream' if profile=='raw' else profile))
            result=validate_stream(output,profile);after=cycles()
            result['c6_scheduler_cycles_per_mib']=(after-before)*1048576/614400
            result['c6_main_stack_free']=int(native('stackfree'))
            result['c6_irq_stack_free']=int(native('irqfree'))
            if profile=='slow':
                result['sink_waits']=int(native('sinkwaits'))
                if not result['sink_waits']:raise ValueError('slow consumer did not apply backpressure')
            if profile=='corrupt':
                injected=re.search(r'SDIO injected=(\d+)',output)
                if not injected or not int(injected[1]):raise ValueError('no INPUT frames were corrupted')
                result['injected']=int(injected[1]);result['c6_wire_bad']=int(native('wbad'))
                if not result['c6_wire_bad']:raise ValueError('target did not reject corrupt frames')
            observations['profiles'][profile]=result
            (args.output/'progress.json').write_text(json.dumps(observations,indent=2)+'\n')
            print(profile+': '+json.dumps(result),flush=True)
        if args.recovery:
            interrupted=command('ipc sdio interrupt',15)
            if 'SDIO reset pending_dma=1 ready_loader=1 ' not in interrupted or 'result=0 live=0 free=4 quarantined=0' not in interrupted:
                raise ValueError('DMA interruption failed: '+interrupted)
            observations['dma_interruption']=interrupted
            boot=command(boot_command,15)
            if 'SDMMC result=0 state=2 mode=1 ' not in boot:raise ValueError('native restart after DMA interruption failed')
            began=time.monotonic();failure=command('ipc sdio console hang',15)
            if 'SDMMC result=-7 ' not in failure:raise ValueError('hung native did not time out')
            while True:
                probe=command('ipc sdio probe',5)
                elapsed=validate_recovery(time.monotonic()-began)
                if 'state=2 mode=2 ' in probe:break
            observations['hung_native_loader_seconds']=elapsed
            boot=command(boot_command,15)
            if 'SDMMC result=0 state=2 mode=1 ' not in boot or native('test')!='1':raise ValueError('explicit native recovery failed')
        observations['passed']=True
    print(json.dumps(observations,indent=2))


if __name__=='__main__':main()
