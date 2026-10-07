#!/usr/bin/env python3
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

decode.py - print the NPU regions a Vela command stream uses.

SPDX-License-Identifier: Apache-2.0
"""
import struct
import sys
from pathlib import Path


def command_stream(raw):
    """Extract one bounded COMMAND_STREAM from Vela driver-action records."""
    if raw[:4] != b"COP1":
        raise ValueError("missing COP1 header")
    stream = None
    offset = 4
    while offset < len(raw):
        if len(raw) - offset < 4:
            raise ValueError("truncated driver action")
        word = struct.unpack_from("<I", raw, offset)[0]
        action, count = word & 0xff, word >> 16
        size = 12 if action == 1 else 4 + count * 4 if action == 2 else 4
        if action not in (1, 2, 5):
            raise ValueError(f"unknown driver action {action}")
        if size > len(raw) - offset:
            raise ValueError("truncated driver-action payload")
        if action == 2:
            if stream is not None or count == 0:
                raise ValueError("expected one nonempty command stream")
            stream = raw[offset + 4:offset + size]
        offset += size
    if stream is None:
        raise ValueError("no COMMAND_STREAM record")
    return stream


def decode_regions(stream, c0, c1):
    """Return REGION commands, rejecting incomplete or invalid instructions."""
    offset, regions = 0, {}
    while offset < len(stream):
        if len(stream) - offset < 4:
            raise ValueError("truncated command")
        word = struct.unpack_from("<I", stream, offset)[0]
        op, mode = word & 0x3ff, (word >> 14) & 3
        if mode == 1:
            if len(stream) - offset < 8:
                raise ValueError("truncated cmd1 operand")
            data = struct.unpack_from("<I", stream, offset + 4)[0]
            name = c1.get(op, f"cmd1_{op:#05x}")
            offset += 8
        elif mode == 0:
            data = word >> 16
            name = c0.get(op, f"cmd0_{op:#05x}")
            offset += 4
        else:
            raise ValueError(f"unsupported command mode {mode}")
        if "REGION" in name:
            regions[name] = data
        if mode == 0 and op == 0:          # NPU_OP_STOP
            return regions
    raise ValueError("missing NPU_OP_STOP")


def main(argv):
    from ethosu.vela.ethos_u55_regs.ethos_u55_regs import cmd0, cmd1
    from ethosu.vela.tflite.Model import Model

    if len(argv) != 2:
        raise ValueError("usage: decode.py model_vela.tflite")
    model = Model.GetRootAs(Path(argv[1]).read_bytes(), 0)
    graph = model.Subgraphs(0)
    raw = bytes(model.Buffers(graph.Tensors(0).Buffer()).DataAsNumpy())
    stream = command_stream(raw)
    print(f"command stream: {len(stream)} bytes")
    regions = decode_regions(stream, {int(c.value): c.name for c in cmd0},
                             {int(c.value): c.name for c in cmd1})
    for name, value in regions.items():
        print(f"  {name} = {value}")
    print("\nregions referenced:", sorted(set(regions.values())))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (ValueError, OSError) as error:
        sys.exit(str(error))
