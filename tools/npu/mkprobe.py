#!/usr/bin/env python3
"""mkprobe.py - author a one-operator int8 model to ask Vela where it lands.

The partition is the number that decides whether a network is worth putting on
this accelerator at all, and it is a property of the OPERATORS rather than of
the model.  So ask one operator at a time: compile the result and read whether
Vela kept it or handed it back to the CPU.

Usage: mkprobe.py <OPERATOR_NAME> <out.tflite> [dim]
"""
import sys

import flatbuffers
from ethosu.vela.tflite import (Model, SubGraph, Tensor, OperatorCode,
                                Operator, Buffer, QuantizationParameters)
from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
from ethosu.vela.tflite.TensorType import TensorType

op_name = sys.argv[1]
out_path = sys.argv[2]
dim = int(sys.argv[3]) if len(sys.argv) > 3 else 32
code = getattr(BuiltinOperator, op_name)

b = flatbuffers.Builder(4096)


def vec(start_fn, items):
    start_fn(b, len(items))
    for x in reversed(items):
        b.PrependUOffsetTRelative(x)
    return b.EndVector()


def ivec(start_fn, items, prepend):
    start_fn(b, len(items))
    for x in reversed(items):
        prepend(x)
    return b.EndVector()


def quant(scale, zp):
    sc = ivec(QuantizationParameters.StartScaleVector, [scale],
              b.PrependFloat32)
    z = ivec(QuantizationParameters.StartZeroPointVector, [zp],
             b.PrependInt64)
    QuantizationParameters.Start(b)
    QuantizationParameters.AddScale(b, sc)
    QuantizationParameters.AddZeroPoint(b, z)
    return QuantizationParameters.End(b)


def tensor(name, shape, buf_idx, scale, zp):
    nm = b.CreateString(name)
    sh = ivec(Tensor.StartShapeVector, shape, b.PrependInt32)
    q = quant(scale, zp)
    Tensor.Start(b)
    Tensor.AddName(b, nm)
    Tensor.AddShape(b, sh)
    Tensor.AddType(b, TensorType.INT8)
    Tensor.AddBuffer(b, buf_idx)
    Tensor.AddQuantization(b, q)
    return Tensor.End(b)


# Softmax and the logistics want a 1/256 output scale at zp -128; for the rest
# the scales are immaterial to where the operator is placed.
BINARY = {"MUL", "ADD", "SUB", "BATCH_MATMUL", "MAXIMUM", "MINIMUM"}
two_in = op_name in BINARY

if op_name == "BATCH_MATMUL":
    shape_a, shape_b, shape_o = [1, dim, dim], [1, dim, dim], [1, dim, dim]
else:
    shape_a = shape_b = shape_o = [1, dim]

t_in = tensor("input", shape_a, 1, 1.0, 0)
t_b = tensor("input_b", shape_b, 3, 1.0, 0) if two_in else None
t_out = tensor("output", shape_o, 2, 1.0 / 256.0, -128)
tensors = vec(SubGraph.StartTensorsVector,
              [t_in, t_out] + ([t_b] if two_in else []))

# Operators whose options the parser dereferences rather than defaults.
opts, opts_type = None, None
if op_name == "SOFTMAX":
    from ethosu.vela.tflite import SoftmaxOptions
    from ethosu.vela.tflite.BuiltinOptions import BuiltinOptions
    SoftmaxOptions.Start(b)
    SoftmaxOptions.AddBeta(b, 1.0)
    opts, opts_type = SoftmaxOptions.End(b), BuiltinOptions.SoftmaxOptions

op_in = ivec(Operator.StartInputsVector,
             [0, 2] if two_in else [0], b.PrependInt32)
op_out = ivec(Operator.StartOutputsVector, [1], b.PrependInt32)
Operator.Start(b)
Operator.AddOpcodeIndex(b, 0)
Operator.AddInputs(b, op_in)
Operator.AddOutputs(b, op_out)
if opts is not None:
    Operator.AddBuiltinOptionsType(b, opts_type)
    Operator.AddBuiltinOptions(b, opts)
operator = Operator.End(b)
operators = vec(SubGraph.StartOperatorsVector, [operator])

sg_in = ivec(SubGraph.StartInputsVector,
             [0, 2] if two_in else [0], b.PrependInt32)
sg_out = ivec(SubGraph.StartOutputsVector, [1], b.PrependInt32)
sg_name = b.CreateString("probe")
SubGraph.Start(b)
SubGraph.AddTensors(b, tensors)
SubGraph.AddInputs(b, sg_in)
SubGraph.AddOutputs(b, sg_out)
SubGraph.AddOperators(b, operators)
SubGraph.AddName(b, sg_name)
subgraph = SubGraph.End(b)
subgraphs = vec(Model.StartSubgraphsVector, [subgraph])

OperatorCode.Start(b)
OperatorCode.AddDeprecatedBuiltinCode(b, code if code < 127 else 127)
OperatorCode.AddBuiltinCode(b, code)
OperatorCode.AddVersion(b, 1)
opcodes = vec(Model.StartOperatorCodesVector, [OperatorCode.End(b)])

bufs = []
for _ in range(4):
    Buffer.Start(b)
    bufs.append(Buffer.End(b))
buffers = vec(Model.StartBuffersVector, bufs)

desc = b.CreateString("tiku npu operator probe")
Model.Start(b)
Model.AddVersion(b, 3)
Model.AddOperatorCodes(b, opcodes)
Model.AddSubgraphs(b, subgraphs)
Model.AddDescription(b, desc)
Model.AddBuffers(b, buffers)
b.Finish(Model.End(b), file_identifier=b"TFL3")

open(out_path, "wb").write(bytes(b.Output()))
print(f"{op_name} -> {out_path}")
