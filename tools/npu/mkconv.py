#!/usr/bin/env python3
"""
Tiku Operating System v0.06
Simple. Ubiquitous. Intelligence, Everywhere.
http://tiku-os.org

Authors: Ambuj Varshney <ambuj@tiku-os.org>

mkconv.py - author an int8 CONV_2D TFLite model with an identity kernel.

Its weights, in the NPU's read-only region, are a 3x3 kernel with only the
centre tap set, each channel to itself.  Every scale is 1.0 with a zero
zero-point, so the output equals the input; the MACs still do 3x3xC per pixel.

SPDX-License-Identifier: Apache-2.0

Usage: mkconv.py <out.tflite> [spatial] [channels]   (defaults 32 and 16)
"""
import struct
import sys

import flatbuffers
from ethosu.vela.tflite import (Model, SubGraph, Tensor, OperatorCode,
                                Operator, Buffer, QuantizationParameters,
                                Conv2DOptions)
from ethosu.vela.tflite.BuiltinOperator import BuiltinOperator
from ethosu.vela.tflite.BuiltinOptions import BuiltinOptions
from ethosu.vela.tflite.TensorType import TensorType
from ethosu.vela.tflite.Padding import Padding
from ethosu.vela.tflite.ActivationFunctionType import ActivationFunctionType

out_path = sys.argv[1]
DIM = int(sys.argv[2]) if len(sys.argv) > 2 else 32
CH = int(sys.argv[3]) if len(sys.argv) > 3 else 16
K = 3

# [out_ch][kh][kw][in_ch], centre tap of each output channel to its own input
w = bytearray(CH * K * K * CH)
for o in range(CH):
    w[((o * K + 1) * K + 1) * CH + o] = 1
bias = struct.pack("<%di" % CH, *([0] * CH))

b = flatbuffers.Builder(1 << 20)


def vec(start_fn, items):
    """Build a flatbuffer vector of table offsets; return its offset."""
    start_fn(b, len(items))
    for x in reversed(items):
        b.PrependUOffsetTRelative(x)
    return b.EndVector()


def ivec(start_fn, items, prepend):
    """Build a flatbuffer vector of scalars with prepend; return its offset."""
    start_fn(b, len(items))
    for x in reversed(items):
        prepend(x)
    return b.EndVector()


def quant():
    """Return QuantizationParameters with scale 1.0 and zero-point 0."""
    sc = ivec(QuantizationParameters.StartScaleVector, [1.0], b.PrependFloat32)
    zp = ivec(QuantizationParameters.StartZeroPointVector, [0],
              b.PrependInt64)
    QuantizationParameters.Start(b)
    QuantizationParameters.AddScale(b, sc)
    QuantizationParameters.AddZeroPoint(b, zp)
    return QuantizationParameters.End(b)


def tensor(name, shape, buf_idx, ttype):
    """Return a Tensor of the given shape, buffer and type, quantised by
    quant()."""
    nm = b.CreateString(name)
    sh = ivec(Tensor.StartShapeVector, shape, b.PrependInt32)
    q = quant()
    Tensor.Start(b)
    Tensor.AddName(b, nm)
    Tensor.AddShape(b, sh)
    Tensor.AddType(b, ttype)
    Tensor.AddBuffer(b, buf_idx)
    Tensor.AddQuantization(b, q)
    return Tensor.End(b)


t_in = tensor("input", [1, DIM, DIM, CH], 1, TensorType.INT8)
t_w = tensor("weights", [CH, K, K, CH], 2, TensorType.INT8)
t_b = tensor("bias", [CH], 3, TensorType.INT32)
t_out = tensor("output", [1, DIM, DIM, CH], 4, TensorType.INT8)
tensors = vec(SubGraph.StartTensorsVector, [t_in, t_w, t_b, t_out])

Conv2DOptions.Start(b)
Conv2DOptions.AddPadding(b, Padding.SAME)
Conv2DOptions.AddStrideW(b, 1)
Conv2DOptions.AddStrideH(b, 1)
Conv2DOptions.AddFusedActivationFunction(b, ActivationFunctionType.NONE)
Conv2DOptions.AddDilationWFactor(b, 1)
Conv2DOptions.AddDilationHFactor(b, 1)
opts = Conv2DOptions.End(b)

op_in = ivec(Operator.StartInputsVector, [0, 1, 2], b.PrependInt32)
op_out = ivec(Operator.StartOutputsVector, [3], b.PrependInt32)
Operator.Start(b)
Operator.AddOpcodeIndex(b, 0)
Operator.AddInputs(b, op_in)
Operator.AddOutputs(b, op_out)
Operator.AddBuiltinOptionsType(b, BuiltinOptions.Conv2DOptions)
Operator.AddBuiltinOptions(b, opts)
operators = vec(SubGraph.StartOperatorsVector, [Operator.End(b)])

sg_in = ivec(SubGraph.StartInputsVector, [0], b.PrependInt32)
sg_out = ivec(SubGraph.StartOutputsVector, [3], b.PrependInt32)
sg_name = b.CreateString("main")
SubGraph.Start(b)
SubGraph.AddTensors(b, tensors)
SubGraph.AddInputs(b, sg_in)
SubGraph.AddOutputs(b, sg_out)
SubGraph.AddOperators(b, operators)
SubGraph.AddName(b, sg_name)
subgraphs = vec(Model.StartSubgraphsVector, [SubGraph.End(b)])

OperatorCode.Start(b)
OperatorCode.AddDeprecatedBuiltinCode(b, BuiltinOperator.CONV_2D)
OperatorCode.AddBuiltinCode(b, BuiltinOperator.CONV_2D)
OperatorCode.AddVersion(b, 1)
opcodes = vec(Model.StartOperatorCodesVector, [OperatorCode.End(b)])

bufs = []
for data in (None, None, bytes(w), bias, None):
    if data is None:
        Buffer.Start(b)
        bufs.append(Buffer.End(b))
    else:
        d = b.CreateByteVector(data)
        Buffer.Start(b)
        Buffer.AddData(b, d)
        bufs.append(Buffer.End(b))
buffers = vec(Model.StartBuffersVector, bufs)

desc = b.CreateString("tiku npu identity conv")
Model.Start(b)
Model.AddVersion(b, 3)
Model.AddOperatorCodes(b, opcodes)
Model.AddSubgraphs(b, subgraphs)
Model.AddDescription(b, desc)
Model.AddBuffers(b, buffers)
b.Finish(Model.End(b), file_identifier=b"TFL3")

open(out_path, "wb").write(bytes(b.Output()))
print(f"{out_path}: {DIM}x{DIM}x{CH}, {K}x{K} identity kernel, "
      f"{len(w)} weight bytes")
