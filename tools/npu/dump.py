#!/usr/bin/env python3
"""Dump a Vela-compiled model: the custom op, its tensors and their buffers."""
import sys
from ethosu.vela.tflite.Model import Model

buf = open(sys.argv[1], "rb").read()
m = Model.GetRootAs(buf, 0)
print(f"version={m.Version()} subgraphs={m.SubgraphsLength()} "
      f"opcodes={m.OperatorCodesLength()} buffers={m.BuffersLength()}")

for i in range(m.OperatorCodesLength()):
    oc = m.OperatorCodes(i)
    cs = oc.CustomCode()
    print(f"  opcode[{i}] builtin={oc.BuiltinCode()} "
          f"custom={cs.decode() if cs else None}")

sg = m.Subgraphs(0)
print(f"\ntensors={sg.TensorsLength()} inputs={list(sg.InputsAsNumpy())} "
      f"outputs={list(sg.OutputsAsNumpy())}")
for i in range(sg.TensorsLength()):
    t = sg.Tensors(i)
    bi = t.Buffer()
    blen = m.Buffers(bi).DataLength()
    off = None
    try:
        off = t.Offset()
    except Exception:
        pass
    print(f"  t[{i}] {t.Name().decode():<28} shape={list(t.ShapeAsNumpy())} "
          f"type={t.Type()} buf={bi} buflen={blen} offset={off}")

for i in range(sg.OperatorsLength()):
    op = sg.Operators(i)
    print(f"\nop[{i}] opcode={op.OpcodeIndex()} "
          f"inputs={list(op.InputsAsNumpy())} outputs={list(op.OutputsAsNumpy())}")
    co = op.CustomOptionsAsNumpy()
    if co is not None and len(co):
        print(f"  custom_options ({len(co)} B): {bytes(co)[:64].hex()}")

# metadata carries the arena/offset map Vela writes
for i in range(m.MetadataLength()):
    md = m.Metadata(i)
    bi = md.Buffer()
    data = m.Buffers(bi).DataAsNumpy()
    n = len(data) if data is not None else 0
    print(f"\nmetadata[{i}] name={md.Name().decode()} buffer={bi} len={n}")
    if n and n <= 128:
        print("   ", bytes(data).hex())
