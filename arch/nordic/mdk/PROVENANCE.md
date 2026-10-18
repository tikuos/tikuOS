# Vendored Nordic MDK register headers (nRF54L15, nRF54LM20A/B, nRF54LV10A)

These headers are the **register-definition subset** of the Nordic MDK,
vendored in-tree so the TikuOS Nordic port builds with no external SDK
dependency (the same approach as `arch/ambiq/cmsis/`).  Four devices share
this directory; `arch/nordic/tiku_nordic_mdk.h` selects the entry wrapper
from the `TIKU_DEVICE_NRF54*` macro.

## Files (nRF54L15)

| File | Purpose |
|------|---------|
| `nrf54l15_types.h`        | Peripheral struct types + field/bit definitions (application core). |
| `nrf54l15_global.h`       | Base-pointer instances (`NRF_UARTE20_S`, `NRF_GRTC_S`, `NRF_FICR_NS`, …). |
| `nrf54l15.h`              | TikuOS entry-point wrapper: includes types + global + `compiler_abstraction.h` + `<stdint.h>`. |
| `nrf54l15_flpr.h`         | FLPR (VPR RISC-V coprocessor) device header: CLIC IRQ map + config. |
| `nrf54l15_flpr_peripherals.h` | FLPR-view peripheral availability/counts. |
| `nrf54l15_flpr_vectors.h` | FLPR CLIC vector-table description. |

## Files (nRF54LM20A and nRF54LM20B)

| File | Purpose |
|------|---------|
| `nrf54lm20a_types.h`      | Peripheral struct types + field/bit definitions (application core). |
| `nrf54lm20a_global.h`     | Base-pointer instances (`NRF_UARTE20_S`, `NRF_GRTC_S`, `NRF_P3_S`, `NRF_FICR_NS`, …). |
| `nrf54lm20a.h`            | TikuOS entry-point wrapper: includes types + global + `compiler_abstraction.h` + `<stdint.h>`. |
| `nrf54lm20b_types.h`      | As `nrf54lm20a_types.h`, plus the Axon NPU block. |
| `nrf54lm20b_global.h`     | As `nrf54lm20a_global.h`, plus `NRF_AXONS_S` at 0x50056000. |
| `nrf54lm20b.h`            | TikuOS entry-point wrapper for the LM20B. |

## Files (nRF54LV10A)

| File | Purpose |
|------|---------|
| `nrf54lv10a_types.h`      | Peripheral struct types + field/bit definitions (application core). |
| `nrf54lv10a_global.h`     | Base-pointer instances (`NRF_UARTE30_S`, `NRF_GRTC_S`, `NRF_FICR_NS`, …); P0 and P1 only, no P2. |
| `nrf54lv10a.h`            | TikuOS entry-point wrapper: includes types + global + `compiler_abstraction.h` + `<stdint.h>`. |

## Shared

| File | Purpose |
|------|---------|
| `compiler_abstraction.h`  | `__ASM` / `__INLINE` / `__WEAK` / `NRF_STATIC_ASSERT` macros (byte-identical between the devices' upstream copies and nrfx v4.3.2, so vendored once). |
| `core_vpr.h`              | VPR core CSR numbers (mtvec/mtvt/mclicbase…), from `mdk/common/`. |
| `riscv_encoding.h`        | RISC-V encoding constants used by `core_vpr.h`, from `mdk/common/`. |

## Source

- Repository: [NordicSemiconductor/nrfx](https://github.com/NordicSemiconductor/nrfx)
- nRF54L15, nRF54LM20A and nRF54LM20B: path `bsp/stable/mdk/nrf54l/<device>/`
  (+ `mdk/common/compiler_abstraction.h`); the nrfx revision is not recorded.
- nRF54LV10A: release tag `v4.3.2` (commit `bdfd422e354b`), path
  `bsp/stable/mdk/nrf54lv10a_types.h` and `bsp/stable/mdk/nrf54lv10a_global.h`
  (MDK datasheet version 0.7.1, from `nrf54lv10a_version.h`).
- License: **BSD-3-Clause** (see the copyright banner at the top of each file —
  "Copyright (c) 2010 - 2026, Nordic Semiconductor ASA. All rights reserved.").

## Local modifications

The nRF54L15 and nRF54LM20A/B files originally did
`#include "../../common/…"`; those were flattened to the bare file names so
the set is self-contained, and `nrf54l15_flpr.h`'s include of `system_nrf.h`
was dropped (SystemInit duties live in the TikuOS startup).  The nRF54LV10A
files are byte-identical to the v4.3.2 release, which already includes
`compiler_abstraction.h` by bare name.  No struct, address, or field
definition was altered in any file.

The nRF54LV10A memory sizes come from the silicon, not from these headers:
FICR.INFO reads 1012 KB of RRAM (`FICR_INFO_RRAM_RRAM_K1012`) and 192 KB of
RAM, while the MDK's `nrf54lv10a_xxaa_application_memory.h`, which is not
vendored, lists 0x1FD000 bytes of RRAM.

## Why no CMSIS core?

`nrf54l15_types.h` self-defines `__IOM` / `__IM` / `__OM` under `#ifndef`, so the
register headers compile with only `<stdint.h>` — no `core_cm33.h` needed. The
Cortex-M33 core intrinsics TikuOS actually uses (NVIC, SCB reset/VTOR, SysTick,
WFI, PRIMASK) are hand-rolled in `arch/nordic/tiku_nordic_core.h`.

The port runs **All-Secure** (no TF-M / SPM), so peripheral access uses the
explicit secure (`_S`, `0x5xxx_xxxx`) aliases.
