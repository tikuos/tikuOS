# Shared compute

The compute HAL provides signed dot products, saturating vector addition
and signed matrix-vector multiplication. Kernel and library callers use
the same contracts. The implementation requires standard C headers and
has no dependency on TikuKits or TikuOS initialization.

## Build and provider

Pass `TIKU_COMPUTE_ENABLE=1` to the firmware build to include the sources
listed in `hal/compute/sources.mk`. `MINIMAL=1` excludes compute sources.
The linker removes unreferenced functions. Host builds can compile
`tiku_compute.c` and `tiku_compute_ref.c` with a C11 compiler.

`TIKU_COMPUTE_BACKEND` selects `reference` (default), `p4` or `mve`.
`tiku_compute_provider(op)` reports the selected provider for the four
defined operations and `TIKU_COMPUTE_PROVIDER_UNKNOWN` for an unknown
operation. The selected provider can execute scalar tails or fallbacks.

| Provider | Selection and implementation |
|---|---|
| `reference` | Standard integer C arithmetic. |
| `p4` | Requires the ESP32-P4 firmware platform and `TIKU_COMPUTE_P4_REV=1`. Uses v1.x PIE instruction encodings. |
| `mve` | Requires compiler integer MVE support, as on Cortex-M55 and Cortex-M85. Uses Arm intrinsics. |

The P4 provider checks the complete operands against the PIE-accessible
0x40000000–0x4fffffff window. Operands outside it, including LP SRAM,
and calls with PIE disabled use reference arithmetic. Unaligned operands
are copied elementwise into aligned stack buffers before vector work;
this reads no surrounding bytes. Dot staging uses 256 bytes for int8
and 512 bytes for int16, plus the compiler's frame. Aligned operands go
directly to PIE. Int16 reductions use at most 504 products per 40-bit
accumulator; int8 reductions use at most 33,554,416. The entry vector
state is restored unless it was already dirty.

MVE int8 reductions use at most 131,056 products per int32 accumulator
and combine chunks in int64. Int16 reductions accumulate in int64.
Both accelerated providers handle incomplete vectors with scalar code.
No hardware-loop instruction encoding is added by the P4 provider.

## Numerical contract

| Operation | Result |
|---|---|
| `tiku_compute_dot_s8` | Exact sum of signed int8 products in int64 storage. |
| `tiku_compute_dot_s16` | Exact sum of signed int16 products in int64 storage. |
| `tiku_compute_add_s32` | Elementwise addition clamped to `INT32_MIN` through `INT32_MAX`. |
| `tiku_compute_matvec_s8` | An int64 result for each signed int8 row dot product. |

Lengths and dimensions are `uint32_t`. Even `UINT32_MAX` products of
`INT16_MIN` squared fit in int64; the public span checks further restrict
the usable length to the target's address space. Matrix row stride counts
int8 elements. It must be at least the column count for nonempty work.
Row padding contributes to the input span but is not read by arithmetic.
The last row needs its column elements, without trailing padding.

## Buffers and errors

Callers provide live readable input buffers and writable output buffers
of the declared extent. Types require natural C alignment, without a
vector alignment requirement. The implementation checks null pointers,
alignment, byte counts against `SIZE_MAX`, and address extents against
`UINTPTR_MAX` before accessing operands. It does not inspect allocation
ownership or memory permissions. The exclusive end address must be
representable. Inputs and outputs must be ordinary memory, not volatile
device registers.

Dot-result storage and matrix output are disjoint from input spans.
Matrix overlap checks include row padding. Vector addition permits output
to equal an input exactly, and rejects partial output/input overlap.
Inputs may overlap one another. The provider does not read outside the
specified element spans.

Zero-length dots require valid result storage and write zero; their input
pointers may be null. Zero-length addition accesses no buffers. A matrix
with zero rows accesses nothing and ignores the other arguments. With
nonzero rows and zero columns, it validates output and fills it with
zeros, ignoring input pointers and stride.

Calls return `TIKU_COMPUTE_OK`, `TIKU_COMPUTE_ERR_PARAM` for null pointers,
alignment or stride, `TIKU_COMPUTE_ERR_RANGE` for unrepresentable extents,
or `TIKU_COMPUTE_ERR_OVERLAP` for prohibited output aliasing. Errors leave
output unchanged. When several arguments are invalid, the validation
order determines the reported error.

## Execution and ownership

Calls are synchronous and reentrant for callers with independent outputs.
They allocate no heap memory and require no caller scratch. They retain
no pointers. Task and worker contexts rely on the port's processor-state
preservation. Callers synchronize buffers shared with a writer. P4 tests
exercise shared calls in the foreground, two yielding workers and a 1 kHz
timer interrupt. P4 traps do not nest. The Apollo510 `compute-mve` test
keeps Q4–Q7 live across worker yields and checks Q0–Q7 and P0 across an
SVC nested inside the timer callback. The foreground, workers and both
exception levels call the shared API. These Arm tests link; their board
execution remains pending.

`TIKU_COMPUTE_TRACE=1` includes per-operation vector and scalar element
counters. These counters are test instrumentation with mutable global
state: callers must serialize traced calls and counter access. Disable
counting with `tiku_compute_trace_enable(0)` before concurrent tests.
Production builds default to tracing disabled and have no mutable compute
globals. Counters measure executed element paths, not elapsed time.

Public headers and reference sources include standard C and compute
headers. Architecture providers may use port headers; the shared layer
does not include architecture headers. Compute implementations cannot
depend on optional services, TikuKits or neural-network vendor libraries.
`tools/check_kernel_dependencies.py` checks includes and known symbol
prefixes in the compute directories.

## Host checks

```bash
make -C TikuBench/tests/host/nonkernel compute dense compute-contract
make -C TikuBench/tests/host/nonkernel BUILD=build/compute-asan \
    EXTRA_CFLAGS='-fsanitize=address -fno-omit-frame-pointer' compute
make -C TikuBench/tests/host/nonkernel BUILD=build/compute-ubsan \
    EXTRA_CFLAGS='-fsanitize=undefined -fno-sanitize-recover=all' compute
```

The contract tests copy the compute sources into a directory without
TikuKits or kernel headers. Closed-form expectations and Python integer
sums check numerical results. Mutated providers exercise the tests for
tails, result width, saturation, stride and overlap errors.

Host PIE and MVE instruction models exercise the production provider
control flow. They do not execute the target instructions. TikuBench's
`compute-p4`, `compute-mve`, `ml-dense` and `ml-chain` categories provide
board tests. The P4 compute, dense and model categories pass on the
FireBeetle 2. Arm providers have host instruction-model checks and firmware
links; those checks do not establish on-device qualification.

At 360 MHz, a batch of 64 aligned SRAM int16 reductions of 4,096 elements
averaged 10.4 µs per shared PIE call and 159.4 µs per raw C reference call.
An offset of one int16 increased the shared call to 129.6 µs because of
staging. These timings include scalar tails and shared-wrapper checks where
applicable, with counter updates disabled. They are workload measurements,
not a guarantee for other lengths, alignments or memory locations.
