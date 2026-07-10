# Vendored Nordic MDK register headers (nRF54L15)

These headers are the **register-definition subset** of the Nordic MDK,
vendored in-tree so the TikuOS Nordic port builds with no external SDK
dependency (the same approach as `arch/ambiq/cmsis/`).

## Files

| File | Purpose |
|------|---------|
| `nrf54l15_types.h`        | Peripheral struct types + field/bit definitions (application core). |
| `nrf54l15_global.h`       | Base-pointer instances (`NRF_UARTE20_S`, `NRF_GRTC_S`, `NRF_FICR_NS`, …). |
| `compiler_abstraction.h`  | `__ASM` / `__INLINE` / `__WEAK` / `NRF_STATIC_ASSERT` macros. |
| `nrf54l15.h`              | TikuOS entry-point wrapper: includes the three above + `<stdint.h>`. |

## Source

- Repository: [NordicSemiconductor/nrfx](https://github.com/NordicSemiconductor/nrfx)
- Path: `bsp/stable/mdk/nrf54l/nrf54l15/` (+ `mdk/compiler_abstraction.h`)
- License: **BSD-3-Clause** (see the copyright banner at the top of each file —
  "Copyright (c) 2010 - 2026, Nordic Semiconductor ASA. All rights reserved.").

## Local modifications

The **only** change from upstream is the include path: the vendored files
originally did `#include "../../common/compiler_abstraction.h"`; that was
flattened to `#include "compiler_abstraction.h"` so the four files form a
self-contained set. No struct, address, or field definition was altered.

## Why no CMSIS core?

`nrf54l15_types.h` self-defines `__IOM` / `__IM` / `__OM` under `#ifndef`, so the
register headers compile with only `<stdint.h>` — no `core_cm33.h` needed. The
Cortex-M33 core intrinsics TikuOS actually uses (NVIC, SCB reset/VTOR, SysTick,
WFI, PRIMASK) are hand-rolled in `arch/nordic/tiku_nordic_core.h`.

The port runs **All-Secure** (no TF-M / SPM), so peripheral access uses the
explicit secure (`_S`, `0x5xxx_xxxx`) aliases.
