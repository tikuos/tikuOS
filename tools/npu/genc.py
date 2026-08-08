#!/usr/bin/env python3
"""Emit the Vela command stream and its arena layout as a C header."""
import struct
import sys

from ethosu.vela.tflite.Model import Model

buf = open(sys.argv[1], "rb").read()
m = Model.GetRootAs(buf, 0)
sg = m.Subgraphs(0)
raw = bytes(m.Buffers(sg.Tensors(0).Buffer()).DataAsNumpy())
# Vela prefixes a "COP1" header carrying the NPU config the stream was built
# for; the hardware queue must be pointed past it or the parser rejects byte 0.
# The payload is a magic word followed by driver-action records, NOT a header
# and a stream: 1 = optimizer config (carries the cfg the stream was built
# for), 2 = command stream with its length in 32-BIT WORDS, 5 = nop.  Slicing
# at a fixed offset lands inside a record and feeds the queue garbage.
assert raw[:4] == b"COP1", raw[:4]
cms, cfg_expect, i = None, 0, 4
while i + 4 <= len(raw):
    word = struct.unpack_from("<I", raw, i)[0]
    action, data = word & 0xFF, (word >> 16) & 0xFFFF
    if action == 1:                      # OPTIMIZER_CONFIG
        cfg_expect = struct.unpack_from("<I", raw, i + 4)[0]
        i += 12
    elif action == 2:                    # COMMAND_STREAM
        cms = raw[i + 4:i + 4 + data * 4]
        i += 4 + data * 4
    elif action == 5:                    # NOP
        i += 4
    else:
        break
assert cms, "no COMMAND_STREAM record"

# OfflineMemoryAllocation: [version, subgraphs, count, off0..offN]
off = None
for i in range(m.MetadataLength()):
    md = m.Metadata(i)
    if md.Name().decode() == "OfflineMemoryAllocation":
        d = bytes(m.Buffers(md.Buffer()).DataAsNumpy())
        n = struct.unpack_from("<I", d, 8)[0]
        off = [struct.unpack_from("<i", d, 12 + 4 * k)[0] for k in range(n)]
assert off is not None, "no OfflineMemoryAllocation metadata"

arena = int(sg.Tensors(2).Shape(0))
in_off, out_off = off[3], off[4]
in_n = 1
for k in range(sg.Tensors(3).ShapeLength()):
    in_n *= int(sg.Tensors(3).Shape(k))
out_n = 1
for k in range(sg.Tensors(4).ShapeLength()):
    out_n *= int(sg.Tensors(4).Shape(k))

in_dim = int(sg.Tensors(3).Shape(1))
out_dim = int(sg.Tensors(4).Shape(1))

lines = [
    "/*",
    " * Tiku Operating System v0.06",
    " * Simple. Ubiquitous. Intelligence, Everywhere.",
    " * http://tiku-os.org",
    " *",
    " * Authors: Ambuj Varshney <ambuj@tiku-os.org>",
    " *",
    " * tiku_npu_maxpool.h - a Vela command stream, generated.",
    " *",
    " * An int8 max-pool whose input and output share one scale, so the",
    " * expected output is a windowed maximum.  Regenerate with",
    " * tools/npu/genc.py.",
    " *",
    " * SPDX-License-Identifier: Apache-2.0",
    " */",
    "",
    "#ifndef TIKU_NPU_MAXPOOL_H_",
    "#define TIKU_NPU_MAXPOOL_H_",
    "",
    "#include <stdint.h>",
    "",
    f"#define TIKU_NPU_MP_ARENA_BYTES   {arena}u",
    f"#define TIKU_NPU_MP_IFM_OFFSET    {in_off}u",
    f"#define TIKU_NPU_MP_OFM_OFFSET    {out_off}u",
    f"#define TIKU_NPU_MP_IFM_BYTES     {in_n}u",
    f"#define TIKU_NPU_MP_OFM_BYTES     {out_n}u",
    f"#define TIKU_NPU_MP_IFM_DIM       {in_dim}u",
    f"#define TIKU_NPU_MP_OFM_DIM       {out_dim}u",
    "#define TIKU_NPU_MP_REGION        1u",
    f"#define TIKU_NPU_MP_CMS_BYTES     {len(cms)}u",
    "/** @brief CONFIG the stream was built for; the silicon must agree. */",
    f"#define TIKU_NPU_MP_CFG_EXPECT    0x{cfg_expect:08x}ul",
    "",
    "static const uint8_t tiku_npu_mp_cms[TIKU_NPU_MP_CMS_BYTES] = {",
]
for i in range(0, len(cms), 12):
    row = ", ".join(f"0x{b:02x}" for b in cms[i:i + 12])
    lines.append(f"    {row},")
lines += ["};", "", "#endif /* TIKU_NPU_MAXPOOL_H_ */", ""]

open(sys.argv[2], "w").write("\n".join(lines))
print(f"arena={arena} ifm@{in_off}({in_n}B) ofm@{out_off}({out_n}B) "
      f"cms={len(cms)}B -> {sys.argv[2]}")
