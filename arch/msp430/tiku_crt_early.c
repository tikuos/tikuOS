/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - early-boot patch: disable the WDT before crt0 BSS init.
 *
 * crt0 zeroes .upper.bss before main() runs, and clearing a large .upper.bss
 * can outlast the reset-default WDT interval, which then resets the part
 * before main() on every boot.  This hook stops the WDT first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"

#ifdef PLATFORM_MSP430

#include <msp430.h>

/*
 * Place this in `.crt_0050early`. Sections sort lexicographically:
 *   .crt_0000start          (toolchain — set SP)
 *   .crt_0050early          (this file  — disable WDT)              <-- here
 *   .crt_0100init_bss       (toolchain — zero .lower.bss / .bss)
 *   .crt_0200init_highbss   (toolchain — zero .upper.bss)
 *   .crt_0300movedata       (toolchain — copy .lower.data / .data)
 *   .crt_0400move_highdata  (toolchain — copy .upper.data)
 *   .crt_..._call_main      (toolchain — call main())
 *
 * The `naked` attribute strips the prologue/epilogue. The single
 * inline-asm instruction is the entire function body. No RET is
 * emitted, so execution falls through to .crt_0100init_bss, as the
 * toolchain's own startup fragments do.
 *
 * `used` keeps the linker from gc-ing this since nothing in C
 * source ever calls __tiku_crt_early_disable_wdt() by name.
 */
/*
 * The instruction writes WDTPW | WDTHOLD (0x5A00 | 0x0080 = 0x5A80) to
 * WDTCTL at 0x015C, its address on the FR5969, FR5994 and FR6989; the
 * FR2433 branch uses its WDTCTL address, 0x01CC.
 */
__attribute__((naked, used, section(".crt_0050early")))
void __tiku_crt_early_disable_wdt(void)
{
#if defined(TIKU_DEVICE_MSP430FR2433)
    __asm__ volatile("mov.w #0x5A80, &0x01CC" ::: "memory");
#else
    __asm__ volatile("mov.w #0x5A80, &0x015C" ::: "memory");
#endif
}

#endif /* PLATFORM_MSP430 */
