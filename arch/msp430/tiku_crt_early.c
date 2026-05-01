/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - Early-boot patch: disable WDT before crt0 BSS init
 *
 * Why this exists
 * ===============
 * The msp430-elf-gcc startup runs .upper.bss zero-init in
 * __crt0_init_highbss *before* main() gets a chance to call
 * tiku_watchdog_off(). At FR6989's POR-default DCO of 8 MHz
 * (CSCTL1 reset → DCOFSEL_3, DCORSEL=0 → 8 MHz; MCLK and SMCLK
 * both run from DCOCLK / 1 at reset), the WDT's POR-default
 * timeout is only:
 *
 *     SMCLK / 32768 = 32768 / 8 MHz = 4.10 ms
 *
 * Zeroing 6.8 KB of HIFRAM at ~5 cycles/byte takes about 4.25 ms
 * — past the WDT window. The chip resets mid-init, restarts crt0,
 * resets again, and boot-loops silently. UART isn't initialised
 * until main() runs, so even `make monitor` shows nothing.
 *
 * Confirmed on MSP430FR6989. See
 * `kintsugi/fr6989_hifram_bss_volume_crash.md` for the symptom,
 * the bisection (works at 2 KB, hangs at 6.8 KB), and the math
 * spelt out in full.
 *
 * How the patch works
 * ===================
 * msp430-elf-gcc orders .crt_* sections lexicographically and the
 * linker `KEEP (*(SORT(.crt_*)))` lays them out contiguously in
 * .text. The startup phases (set SP, init bss, init high bss,
 * move data, ..., call_main) are inlined as a fall-through chain
 * — no RET separates them; execution simply runs off the end of
 * one section into the next.
 *
 * We slot in at `.crt_0050early`, lexicographically between the
 * existing `.crt_0000start` and `.crt_0100init_bss`. The function
 * is `naked` so the compiler emits no prologue/epilogue, and it
 * deliberately ends WITHOUT a RET so execution falls through to
 * the next .crt section — same convention as the toolchain's
 * own startup phases.
 *
 * Cost: 4 bytes of .text (a single MOV.W immediate-to-absolute
 * instruction). No data, no stack use, no SR change. Safe on
 * every MSP430FR-series part — `WDTPW | WDTHOLD` is the documented
 * stop sequence and works the same way regardless of the chip's
 * BSS layout.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#ifdef PLATFORM_MSP430

#include <msp430.h>

/*
 * Place this in `.crt_0050early`. Sections sort lexicographically:
 *   .crt_0000start          (toolchain — set SP)
 *   .crt_0050early          (us         — disable WDT)              <-- here
 *   .crt_0100init_bss       (toolchain — zero .lower.bss / .bss)
 *   .crt_0200init_highbss   (toolchain — zero .upper.bss)
 *   .crt_0300movedata       (toolchain — copy .lower.data / .data)
 *   .crt_0400move_highdata  (toolchain — copy .upper.data)
 *   .crt_..._call_main      (toolchain — call main())
 *
 * The `naked` attribute strips the prologue/epilogue. The single
 * inline-asm instruction is the entire function body. No RET is
 * emitted, so execution falls through to .crt_0100init_bss exactly
 * the way the toolchain's own startup chain works.
 *
 * `used` keeps the linker from gc-ing this since nothing in C
 * source ever calls __tiku_crt_early_disable_wdt() by name.
 */
/*
 * The WDTCTL register lives at address 0x015C on every MSP430.
 * `WDTPW | WDTHOLD` = 0x5A00 | 0x0080 = 0x5A80. We bake both
 * constants into the asm template directly so the constraint
 * machinery doesn't have to deal with `&WDTCTL` vs `#WDTCTL`
 * ambiguity in the immediate/absolute operand slots.
 */
__attribute__((naked, used, section(".crt_0050early")))
void __tiku_crt_early_disable_wdt(void)
{
    __asm__ volatile("mov.w #0x5A80, &0x015C" ::: "memory");
}

#endif /* PLATFORM_MSP430 */
