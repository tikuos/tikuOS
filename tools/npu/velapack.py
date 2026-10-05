#!/usr/bin/env python3
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

velapack.py - pack a Vela-compiled model into a store-resident .eth file.

Writes a .tflite's command stream, weights and arena layout to the file that
tiku_ra8p1_npu_load() maps from /data.  The stream addresses memory through
the NPU's region base pointers, so the file needs no relocation.

SPDX-License-Identifier: Apache-2.0

Layout, little-endian:

    0   magic "TKNP"      20  ifm_dim  u16, ofm_dim u16
    4   version u16       24  cfg      u32   NPU CONFIG it was built for
    6   kind     u8       28  cms_len  u32
    7   channels u8       32  wts_len  u32   the read-only blob
    8   arena    u32      36  reserved u32
    12  ifm_off  u32      40  command stream, then the weights
    16  ofm_off  u32

`kind` names the expected output the firmware checks the NPU against: 0 is
the max-pool, 1 the identity convolution, whose output equals its input.

Usage: velapack.py <model_vela.tflite> <out.eth>
"""
import struct
import sys

from ethosu.vela.tflite.Model import Model

MAGIC = b"TKNP"
VERSION = 2
HDR_FMT = "<4sHBBIIIHHIIII"
KIND_MAXPOOL, KIND_IDENTITY_CONV = 0, 1

buf = open(sys.argv[1], "rb").read()
m = Model.GetRootAs(buf, 0)
sg = m.Subgraphs(0)
raw = bytes(m.Buffers(sg.Tensors(0).Buffer()).DataAsNumpy())

# Walks the driver-action records as tools/npu/genc.py does.
assert raw[:4] == b"COP1", raw[:4]
cms, cfg, i = None, 0, 4
while i + 4 <= len(raw):
    word = struct.unpack_from("<I", raw, i)[0]
    action, data = word & 0xFF, (word >> 16) & 0xFFFF
    if action == 1:
        cfg = struct.unpack_from("<I", raw, i + 4)[0]
        i += 12
    elif action == 2:
        cms = raw[i + 4:i + 4 + data * 4]
        i += 4 + data * 4
    elif action == 5:
        i += 4
    else:
        break
assert cms, "no COMMAND_STREAM record"

off = None
for k in range(m.MetadataLength()):
    md = m.Metadata(k)
    if md.Name().decode() == "OfflineMemoryAllocation":
        d = bytes(m.Buffers(md.Buffer()).DataAsNumpy())
        n = struct.unpack_from("<I", d, 8)[0]
        off = [struct.unpack_from("<i", d, 12 + 4 * j)[0] for j in range(n)]
assert off is not None, "no OfflineMemoryAllocation metadata"

arena = int(sg.Tensors(2).Shape(0))
# DataAsNumpy() of an empty buffer returns 0, not an array, so the length
# is checked first.
_wb = m.Buffers(sg.Tensors(1).Buffer())
wts = bytes(_wb.DataAsNumpy()) if _wb.DataLength() else b""

# A model with weights is packed as the identity convolution, one without as
# the max-pool.
in_t, out_t = sg.Tensors(3), sg.Tensors(4)
if wts:
    kind = KIND_IDENTITY_CONV
    channels = int(in_t.Shape(in_t.ShapeLength() - 1))
else:
    kind, channels = KIND_MAXPOOL, 1

hdr = struct.pack(HDR_FMT, MAGIC, VERSION, kind, channels, arena,
                  off[3], off[4], int(in_t.Shape(1)), int(out_t.Shape(1)),
                  cfg, len(cms), len(wts), 0)
open(sys.argv[2], "wb").write(hdr + cms + wts)
print(f"{sys.argv[2]}: kind={kind} arena={arena} ifm@{off[3]} ofm@{off[4]} "
      f"dim={int(in_t.Shape(1))} ch={channels} cms={len(cms)}B "
      f"wts={len(wts)}B total={len(hdr) + len(cms) + len(wts)}B")
