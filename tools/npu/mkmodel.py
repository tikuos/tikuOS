#!/usr/bin/env python3
"""Author a minimal int8 MAX_POOL_2D TFLite model.

Deliberately the simplest network whose reference output cannot be argued
with: input and output carry the SAME scale and a zero zero-point, so the
operator is a plain windowed maximum over int8 and requantisation cannot
enter.  Anything richer would make a mismatch ambiguous between "the NPU is
wrong" and "my reference arithmetic is wrong".

Built with the flatbuffer classes Vela already bundles, so this needs no
TensorFlow.
"""
import sys
import flatbuffers
from ethosu.vela.tflite import (Model, SubGraph, Tensor, OperatorCode,
                                Operator, Buffer, QuantizationParameters,
                                Pool2DOptions)
from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
from ethosu.vela.tflite.BuiltinOptions import BuiltinOptions
from ethosu.vela.tflite.TensorType import TensorType
from ethosu.vela.tflite.Padding import Padding
from ethosu.vela.tflite.ActivationFunctionType import ActivationFunctionType

IN_H = IN_W = int(sys.argv[2]) if len(sys.argv) > 2 else 8
OUT_H = OUT_W = IN_H // 2
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


def quant():
    """scale = 1.0, zero_point = 0 -> the identity mapping int8 <-> value."""
    sc = ivec(QuantizationParameters.StartScaleVector, [1.0], b.PrependFloat32)
    zp = ivec(QuantizationParameters.StartZeroPointVector, [0],
              b.PrependInt64)
    QuantizationParameters.Start(b)
    QuantizationParameters.AddScale(b, sc)
    QuantizationParameters.AddZeroPoint(b, zp)
    return QuantizationParameters.End(b)


def tensor(name, shape, buf_idx):
    nm = b.CreateString(name)
    sh = ivec(Tensor.StartShapeVector, shape, b.PrependInt32)
    q = quant()
    Tensor.Start(b)
    Tensor.AddName(b, nm)
    Tensor.AddShape(b, sh)
    Tensor.AddType(b, TensorType.INT8)
    Tensor.AddBuffer(b, buf_idx)
    Tensor.AddQuantization(b, q)
    return Tensor.End(b)


t_in = tensor("input", [1, IN_H, IN_W, 1], 1)
t_out = tensor("output", [1, OUT_H, OUT_W, 1], 2)
tensors = vec(SubGraph.StartTensorsVector, [t_in, t_out])

Pool2DOptions.Start(b)
Pool2DOptions.AddPadding(b, Padding.VALID)
Pool2DOptions.AddStrideW(b, 2)
Pool2DOptions.AddStrideH(b, 2)
Pool2DOptions.AddFilterWidth(b, 2)
Pool2DOptions.AddFilterHeight(b, 2)
Pool2DOptions.AddFusedActivationFunction(b, ActivationFunctionType.NONE)
opts = Pool2DOptions.End(b)

op_in = ivec(Operator.StartInputsVector, [0], b.PrependInt32)
op_out = ivec(Operator.StartOutputsVector, [1], b.PrependInt32)
Operator.Start(b)
Operator.AddOpcodeIndex(b, 0)
Operator.AddInputs(b, op_in)
Operator.AddOutputs(b, op_out)
Operator.AddBuiltinOptionsType(b, BuiltinOptions.Pool2DOptions)
Operator.AddBuiltinOptions(b, opts)
operator = Operator.End(b)
operators = vec(SubGraph.StartOperatorsVector, [operator])

sg_in = ivec(SubGraph.StartInputsVector, [0], b.PrependInt32)
sg_out = ivec(SubGraph.StartOutputsVector, [1], b.PrependInt32)
sg_name = b.CreateString("main")
SubGraph.Start(b)
SubGraph.AddTensors(b, tensors)
SubGraph.AddInputs(b, sg_in)
SubGraph.AddOutputs(b, sg_out)
SubGraph.AddOperators(b, operators)
SubGraph.AddName(b, sg_name)
subgraph = SubGraph.End(b)
subgraphs = vec(Model.StartSubgraphsVector, [subgraph])

OperatorCode.Start(b)
OperatorCode.AddDeprecatedBuiltinCode(b, BuiltinOperator.MAX_POOL_2D)
OperatorCode.AddBuiltinCode(b, BuiltinOperator.MAX_POOL_2D)
OperatorCode.AddVersion(b, 1)
opcode = OperatorCode.End(b)
opcodes = vec(Model.StartOperatorCodesVector, [opcode])

bufs = []
for _ in range(3):
    Buffer.Start(b)
    bufs.append(Buffer.End(b))
buffers = vec(Model.StartBuffersVector, bufs)

desc = b.CreateString("tiku npu n2 maxpool")
Model.Start(b)
Model.AddVersion(b, 3)
Model.AddOperatorCodes(b, opcodes)
Model.AddSubgraphs(b, subgraphs)
Model.AddDescription(b, desc)
Model.AddBuffers(b, buffers)
b.Finish(Model.End(b), file_identifier=b"TFL3")

open(sys.argv[1], "wb").write(bytes(b.Output()))
print(f"wrote {sys.argv[1]}: {len(bytes(b.Output()))} bytes")
