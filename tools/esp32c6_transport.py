#!/usr/bin/env python3
#
# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# esp32c6_transport.py - Exclusive P4 console and checked C6 bridge exchanges.
#
# SPDX-License-Identifier: Apache-2.0
"""Carry loader, factory and remote shell frames through the owned P4 console."""
import json
from pathlib import Path
import re
import sys
import time
from esp32c6_resident_update import packet,response,acquire_prompt
from esp32c6_sdio_qualify import command_complete


class Console:
    """Own one identified P4 console until close and retain its text/frame evidence."""
    def __init__(self,port,serial_number,directory):
        import serial
        from serial.tools import list_ports
        from esp_usb_console import require_idle_usb_port,usb_console
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'TikuBench'))
        from tikubench.core.frames import Splitter,frame
        ports=list(list_ports.comports());require_idle_usb_port(port,ports)
        real=Path(port).resolve()
        if not any(Path(p.device).resolve()==real and p.serial_number==serial_number for p in ports):
            raise ValueError('P4 USB serial identity mismatch')
        self.wire=usb_console(serial,port);self.wire.open()
        try:self.log=(Path(directory)/'console.log').open('a')
        except Exception:
            self.wire.close();raise
        self.split=Splitter(markers=(0xf4,0xf5,0xf6));self.frame=frame
        self.text='';self.incoming=[];self.remote_incoming=[];self.sequence=0;self.active=False
        try:acquire_prompt(self.wire,self.pump,lambda:'tikuOS:/> ' in self.text)
        except Exception:
            self.wire.close();self.log.close();raise

    def pump(self):
        self.split.feed(self.wire.read(self.wire.in_waiting or 1))
        got=self.split.take_text().decode(errors='replace')
        self.text+=got;self.log.write(got);self.log.flush()
        for mark,data in self.split.take_frames():
            if mark==0xf5:self.incoming.append(response(data))
            elif mark==0xf6:self.remote_incoming.append(bytes(data))

    def wait(self,condition,timeout=15):
        until=time.monotonic()+timeout
        while not condition():
            if time.monotonic()>=until:raise TimeoutError(self.text[-1600:])
            self.pump()

    def command(self,value,timeout=15):
        start=len(self.text);self.wire.write((value+'\n').encode())
        self.wait(lambda:command_complete(self.text[start:],value),timeout)
        return self.text[start:]

    def native(self,value):
        text=self.command('ipc sdio console "'+value+'"')
        found=re.search(r'C6 reply: ([^\r\n]+)',text)
        if not found or 'SDMMC result=0 state=2 mode=1 ' not in text:
            raise ValueError('native reply failed: '+text)
        return found[1]

    def bridge(self,kind='bridge'):
        self.incoming=[];start=len(self.text)
        self.wire.write(('ipc sdio '+kind+'\n').encode())
        self.wait(lambda:any(p['sequence']==0 for p in self.incoming) or
                  command_complete(self.text[start:],'ipc sdio '+kind))
        found=next((p for p in self.incoming if p['sequence']==0),None)
        if not found or found['status']:raise ValueError('bridge discovery: '+self.text[start:])
        self.incoming.remove(found);self.active=True;self.kind=kind
        return found['payload']

    def send(self,op,offset=0,length=0,payload=b''):
        self.sequence+=1
        self.wire.write(self.frame(0xf5,packet(op,self.sequence,offset,length,payload)))
        return self.sequence

    def rpc(self,op,offset=0,length=0,payload=b''):
        start=len(self.text)
        seq=self.send(op,offset,length,payload)
        self.wait(lambda:any(p['sequence']==seq for p in self.incoming) or
                  command_complete(self.text[start:],'rpc'),30)
        p=next((p for p in self.incoming if p['sequence']==seq),None)
        if p is None:
            self.active=False;raise ValueError('bridge closed: '+self.text[start:])
        self.incoming.remove(p)
        if (p['op'],p['offset'],p['length'],p['status'])!=(op,offset,length,0):
            raise ValueError('C6 RPC refused: '+str({k:v for k,v in p.items() if k!='payload'}))
        return p['payload']

    def read(self,offset,size):
        out=bytearray()
        for at in range(0,size,384):
            count=min(384,size-at);block=self.rpc(2,offset+at,count)
            if len(block)!=count:raise ValueError('short flash read')
            out.extend(block)
        return bytes(out)

    def stop_factory(self):
        start=len(self.text);self.send(20)
        self.wait(lambda:'C6 loader bridge done' in self.text[start:] and
                  command_complete(self.text[start:],'ipc sdio factory'))
        self.active=False

    def abort_factory(self):
        """Cancel the factory job through the same owner's confirmed DMA reset path."""
        start=len(self.text);self.send(21)
        self.wait(lambda:'C6 loader bridge done' in self.text[start:] and
                  command_complete(self.text[start:],'ipc sdio factory'))
        self.active=False

    def end_loader(self,op=17):
        start=len(self.text);self.rpc(op)
        self.wait(lambda:'C6 loader bridge done' in self.text[start:] and
                  command_complete(self.text[start:],'ipc sdio bridge'))
        self.active=False

    def close(self):
        self.wire.close();self.log.close()


def varint(value):
    """Encode an unsigned 32-bit protobuf integer."""
    if not 0<=value<=0xffffffff:raise ValueError('varint bounds')
    out=bytearray()
    while value>=128:out.append((value&127)|128);value>>=7
    out.append(value);return bytes(out)


def fields(data):
    """Decode bounded scalar and byte fields used by the factory RPC service."""
    at=0;result={}
    def integer():
        nonlocal at
        value=0
        for shift in range(0,35,7):
            if at>=len(data):raise ValueError('truncated protobuf integer')
            b=data[at];at+=1
            if shift==28 and b&0xf0:raise ValueError('protobuf integer overflow')
            value|=(b&127)<<shift
            if not b&128:return value
        raise ValueError('protobuf integer overflow')
    while at<len(data):
        tag=integer();number=tag>>3
        if not number or number in result:raise ValueError('protobuf duplicate/empty field')
        if tag&7==0:result[number]=integer()
        elif tag&7==2:
            n=integer()
            if n>len(data)-at:raise ValueError('protobuf byte bounds')
            result[number]=data[at:at+n];at+=n
        else:raise ValueError('unsupported factory protobuf wire type')
    return result


def factory_request(ident,uid,chunk=b''):
    """Encode factory MAC/version or bounded OTA begin/data/end requests."""
    if ident not in (257,350,272,273,274) or not uid or len(chunk)>384:
        raise ValueError('factory request bounds')
    if (ident==273)!=bool(chunk):raise ValueError('factory chunk presence')
    nested=b'\x0a'+varint(len(chunk))+chunk if chunk else b''
    body=b'\x08\x01\x10'+varint(ident)+b'\x18'+varint(uid)+varint(ident<<3|2)+varint(len(nested))+nested
    header=bytearray(24);header[0]=3;header[4]=12
    header[12:22]=b'\x01\x06\x00RPCRsp\x02'
    header[2:4]=(len(body)+12).to_bytes(2,'little');header[22:24]=len(body).to_bytes(2,'little')
    return bytes(header)+body


def factory_status(data,ident,uid):
    """Match the complete factory response identity before accepting its status."""
    if (len(data)<24 or data[0]!=3 or data[4:6]!=b'\x0c\x00' or
            int.from_bytes(data[2:4],'little')+12!=len(data) or
            data[12:22]!=b'\x01\x06\x00RPCRsp\x02' or
            int.from_bytes(data[22:24],'little')+24!=len(data)):
        return None
    items=fields(data[24:]);wanted=ident+256
    if items.get(1)!=2 or items.get(2)!=wanted or items.get(3,0)!=uid:return None
    if wanted not in items:return None
    return fields(items[wanted]).get(1,0)


class Factory:
    """Check factory identity before accepting any OTA erase/write operation."""
    def __init__(self,console,mac):
        self.console=console;self.uid=0
        for attempt in range(2):
            try:console.bridge('factory');break
            except ValueError:
                if attempt:raise
        self.request(257)
        raw=bytearray(15);raw[0]=4;raw[2]=3;raw[4]=12;raw[11]=1;raw[12:14]=b'\x09\x10'
        reply=console.rpc(19,payload=bytes(raw))
        for _ in range(5):
            if len(reply)==25 and reply[:6]==b'\x04\x00\x0d\x00\x0c\x00' and reply[12:19]==b'\x04\x0e\x0a\x01\x09\x10\x00':break
            reply=console.rpc(19)
        else:raise ValueError('factory Bluetooth identity missing')
        got=bytes(reversed(reply[19:25]))
        expected=(int.from_bytes(bytes.fromhex(mac.replace(':','')),'big')+2).to_bytes(6,'big')
        if got!=expected:raise ValueError('factory C6 MAC mismatch')

    def request(self,ident,chunk=b''):
        self.uid+=1;uid=self.uid
        reply=self.console.rpc(19,payload=factory_request(ident,uid,chunk))
        for _ in range(5):
            status=factory_status(reply,ident,uid)
            if status is not None:
                if status:raise ValueError('factory OTA status '+hex(status))
                return reply
            reply=self.console.rpc(19)
        raise ValueError('factory response identity missing')

    def install(self,image,interrupt=None,progress=None):
        if interrupt=='before-erase':
            self.console.abort_factory();raise InterruptedError('before erase')
        self.request(272)
        for off in range(0,len(image),384):
            self.request(273,image[off:off+384])
            if progress:progress(min(off+384,len(image)))
            if interrupt=='payload' and off>=len(image)//2:
                self.console.abort_factory();raise InterruptedError('during payload')
        if interrupt=='selection':
            self.uid+=1;start=len(self.console.text)
            try:self.console.rpc(19,offset=1,payload=factory_request(274,self.uid))
            except ValueError:
                if 'C6 factory cut=1' not in self.console.text[start:]:raise
                raise InterruptedError('after OTA_END transmit completion')
            raise ValueError('factory transmission cut did not trigger')
        self.request(274);self.console.stop_factory()
        # Factory OTA restarts the peer five seconds after its final response.
        until=time.monotonic()+8
        while time.monotonic()<until:self.console.pump()
