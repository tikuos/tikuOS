# Vendored Nordic MDK register headers (nRF54L15 + nRF54LM20A)

These headers are the **register-definition subset** of the Nordic MDK,
vendored in-tree so the TikuOS Nordic port builds with no external SDK
dependency (the same approach as `arch/ambiq/cmsis/`).  Two devices share this
directory; the correct entry wrapper is selected by
`arch/nordic/tiku_nordic_mdk.h` from the `TIKU_DEVICE_NRF54*` macro.

## Files (nRF54L15)

| File | Purpose |
|------|---------|
| `nrf54l15_types.h`        | Peripheral struct types + field/bit definitions (application core). |
| `nrf54l15_global.h`       | Base-pointer instances (`NRF_UARTE20_S`, `NRF_GRTC_S`, `NRF_FICR_NS`, …). |
| `nrf54l15.h`              | TikuOS entry-point wrapper: includes types + global + `compiler_abstraction.h` + `<stdint.h>`. |
| `nrf54l15_flpr.h`         | FLPR (VPR RISC-V coprocessor) device header: CLIC IRQ map + config. |
| `nrf54l15_flpr_peripherals.h` | FLPR-view peripheral availability/counts. |
| `nrf54l15_flpr_vectors.h` | FLPR CLIC vector-table description. |

## Files (nRF54LM20A)

| File | Purpose |
|------|---------|
| `nrf54lm20a_types.h`      | Peripheral struct types + field/bit definitions (application core). |
| `nrf54lm20a_global.h`     | Base-pointer instances (`NRF_UARTE20_S`, `NRF_GRTC_S`, `NRF_P3_S`, `NRF_FICR_NS`, …). |
| `nrf54lm20a.h`            | TikuOS entry-point wrapper: includes types + global + `compiler_abstraction.h` + `<stdint.h>`. |

## Shared

| File | Purpose |
|------|---------|
| `compiler_abstraction.h`  | `__ASM` / `__INLINE` / `__WEAK` / `NRF_STATIC_ASSERT` macros (byte-identical between both devices' upstream copies, so vendored once). |
| `core_vpr.h`              | VPR core CSR numbers (mtvec/mtvt/mclicbase…), from `mdk/common/`. |
| `riscv_encoding.h`        | RISC-V encoding constants used by `core_vpr.h`, from `mdk/common/`. |

## Source

- Repository: [NordicSemiconductor/nrfx](https://github.com/NordicSemiconductor/nrfx)
- Path: `bsp/stable/mdk/nrf54l/nrf54l15/` and `bsp/stable/mdk/nrf54l/nrf54lm20a/`
  (+ `mdk/common/compiler_abstraction.h`)
- License: **BSD-3-Clause** (see the copyright banner at the top of each file —
  "Copyright (c) 2010 - 2026, Nordic Semiconductor ASA. All rights reserved.").

## Local modifications

The **only** change from upstream is the include path: the vendored files
originally did `#include "../../common/…"`; those were flattened to the bare
file names so the set is self-contained, and `nrf54l15_flpr.h`'s include of
`system_nrf.h` was dropped (SystemInit duties live in the TikuOS startup).
No struct, address, or field definition was altered.

## Why no CMSIS core?

`nrf54l15_types.h` self-defines `__IOM` / `__IM` / `__OM` under `#ifndef`, so the
register headers compile with only `<stdint.h>` — no `core_cm33.h` needed. The
Cortex-M33 core intrinsics TikuOS actually uses (NVIC, SCB reset/VTOR, SysTick,
WFI, PRIMASK) are hand-rolled in `arch/nordic/tiku_nordic_core.h`.

The port runs **All-Secure** (no TF-M / SPM), so peripheral access uses the
explicit secure (`_S`, `0x5xxx_xxxx`) aliases.
