/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - early-boot patch: disable the WDT before crt0 BSS init.
 *
 * The toolchain zeroes .upper.bss before main() can stop the watchdog, and at the
 * POR-default 8 MHz the WDT window is 4.10 ms while zeroing 6.8 KB of HIFRAM takes
 * ~4.25 ms -- a silent boot loop, confirmed on MSP430FR6989.
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
