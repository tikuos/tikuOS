#!/usr/bin/env python3
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

decode.py - print the NPU regions a Vela command stream uses.

Decodes the command stream in tensor 0's buffer of a Vela-compiled .tflite
(argv[1]) far enough to print each REGION command, up to NPU_OP_STOP: the
regions are the base pointers to program before the stream runs.

SPDX-License-Identifier: Apache-2.0
"""
import struct
import sys

from ethosu.vela.ethos_u55_regs.ethos_u55_regs import cmd0, cmd1
from ethosu.vela.tflite.Model import Model

buf = open(sys.argv[1], "rb").read()
m = Model.GetRootAs(buf, 0)
sg = m.Subgraphs(0)
cms = bytes(m.Buffers(sg.Tensors(0).Buffer()).DataAsNumpy())
print(f"command stream: {len(cms)} bytes")

c0 = {int(c.value): c.name for c in cmd0}
c1 = {int(c.value): c.name for c in cmd1}

i = 0
regions = {}
while i + 4 <= len(cms):
    word = struct.unpack_from("<I", cms, i)[0]
    op = word & 0x3FF
    is_cmd1 = (word >> 14) & 0x3
    if is_cmd1 == 1:                       # cmd1: 32-bit opcode + 32-bit data
        data = struct.unpack_from("<I", cms, i + 4)[0]
        name = c1.get(op, f"cmd1_{op:#05x}")
        i += 8
    else:                                  # cmd0: opcode + 16-bit param
        data = (word >> 16) & 0xFFFF
        name = c0.get(op, f"cmd0_{op:#05x}")
        i += 4
    if "REGION" in name:
        regions[name] = data
        print(f"  {name} = {data}")
    if name in ("NPU_OP_STOP", "cmd0_0x001"):
        break

print("\nregions referenced:", sorted(set(regions.values())))
