# arch/ambiq/cmsis — vendored register headers

These are **register-definition headers only** — the Apollo510 peripheral
register map plus the ARM Cortex-M55 core peripherals. They contain **no
AmbiqSuite HAL/BSP logic**, only `struct`/bitfield/address definitions, which are
hardware facts. They are vendored here so `arch/ambiq` is fully self-contained:
the build references only in-tree files plus the MRAM bootrom blob, never an
external AmbiqSuite tree.

Each file keeps its upstream copyright/license header intact.

## Ambiq device header — © Ambiq Micro, BSD-3-Clause

| file                 | upstream source |
|----------------------|-----------------|
| `apollo510.h`        | AmbiqSuite `release_sdk5p1p0` → `CMSIS/AmbiqMicro/Include/apollo510.h` |
| `system_apollo510.h` | AmbiqSuite `release_sdk5p1p0` → `CMSIS/AmbiqMicro/Include/system_apollo510.h` |

`apollo510.h` is machine-generated (CMSIS-SVD) from the formal register
description `pack/SVD/apollo510.svd` in that same SDK. It defines **all 30
Apollo510 peripherals** (PWRCTRL, GPIO, STIMER, UART0, IOM0, MSPI0, ADC, TIMER,
…), so new drivers need no header edits. It is kept vendored — rather than
regenerated — so it is byte-identical to the file the arch code was validated
against; it can be regenerated from the SVD with `svdconv` — see below.

## Register-map source — `apollo510.svd.gz` (© Ambiq Micro)

`apollo510.svd.gz` is the gzipped CMSIS-SVD that `apollo510.h` was generated
from — the formal, machine-readable description of every Apollo510 register. The
build never reads it; it is kept as the upstream "blueprint" so the header can be
regenerated from primary source rather than trusted as a copy.

    AmbiqSuite release_sdk5p1p0 -> pack/SVD/apollo510.svd  (10.5 MB raw, 356 KB gz)

To regenerate the header (needs `svdconv` from the Open-CMSIS-Pack cmsis-toolbox):

    gunzip -k arch/ambiq/cmsis/apollo510.svd.gz      # -> apollo510.svd
    svdconv  apollo510.svd --generate=header         # -> apollo510.h

Byte-exact reproduction depends on Ambiq's original generator options, but the
register / struct / bit-field definitions are equivalent. The checked-in
`apollo510.h` remains the canonical, hardware-validated copy.

## ARM CMSIS-Core for Cortex-M55 — © ARM Limited, Apache-2.0

CMSIS-Core v5.6, the GCC include closure. ARM's standard, hand-maintained
Cortex-M55 core header — it is **not** SVD-derived, so it is vendored as-is.

| file                | role |
|---------------------|------|
| `core_cm55.h`       | SCB, NVIC, SysTick, MPU, cache maintenance, `MEMSYSCTL` |
| `cmsis_gcc.h`       | GCC intrinsics (`__DSB`, `__ISB`, `__WFI`, cache ops, …) |
| `cmsis_compiler.h`  | compiler dispatch (selects `cmsis_gcc.h` under GCC) |
| `cmsis_version.h`   | CMSIS version macros |
| `mpu_armv8.h`       | ARMv8-M MPU helpers (pulled by core_cm55.h) |
| `pmu_armv8.h`       | ARMv8-M PMU helpers (pulled by core_cm55.h) |
| `cachel1_armv7.h`   | L1 cache maintenance helpers (pulled by core_cm55.h) |

## Updating

- Ambiq files: re-copy from a newer AmbiqSuite SDK's `CMSIS/AmbiqMicro/Include/`
  (or regenerate `apollo510.h` from its `pack/SVD/apollo510.svd` via `svdconv`).
- ARM files: re-copy the GCC closure from an Open-CMSIS-Pack CMSIS-Core release.

The Makefile points `-I` at this directory only; there is no `AMBIQ_SDK_DIR`.
