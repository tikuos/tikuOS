/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - Apollo 510 common CPU helpers (delays, IDs)
 *
 * Bare-metal delays spin on the Cortex-M SysTick counter — reliable (unlike
 * the Apollo5 DWT, which ticks at 2x the core and broke a DWT-based delay) and
 * sharing the exact clock basis as the system tick, so a delay and a tick can
 * never disagree. Scaled by the SysTick clock = TIKU_MAIN_CPU_HZ = 96 MHz, the
 * full M55 core (SysTick CLKSOURCE=processor). No AmbiqSuite dependency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku.h"              /* TIKU_MAIN_CPU_HZ = 96 MHz SysTick clock */
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"  /* tiku_cpu_ambiq_clock_get_hz (live core clock) */
#include "apollo510.h"         /* CMSIS register map (MCUCTRL CHIPID) -- register header only */

/**
 * @defgroup SYST_REGS SysTick register accessors
 * @brief Cortex-M SysTick (System Control Space) — 24-bit down-counter,
 *        auto-reload. Used as the delay timebase at TIKU_MAIN_CPU_HZ.
 * @{
 */
/** Reload Value Register (24-bit) */
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
/** Current Value Register (24-bit, counts down) */
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
/** Mask for the 24 valid counter bits */
#define SYST_MASK 0x00FFFFFFu
/** @} */

/**
 * @brief Spin-delay for a given number of microseconds
 *
 * Uses the Cortex-M SysTick down-counter as the timebase, scaled by
 * TIKU_MAIN_CPU_HZ (96 MHz). Reliable across sleep and clock changes
 * because it tracks elapsed SysTick ticks rather than loop iterations.
 * Falls back to a NOP spin loop when SysTick is not yet configured
 * (pre-clock-init). Does not depend on AmbiqSuite.
 *
 * SysTick CLKSOURCE=processor, so the counter runs at the full M55 core
 * frequency (96 MHz LP / 192 MHz HP). Scale by the LIVE core clock so the
 * delay stays correct across an LP<->HP perf-mode switch.
 *
 * @param us  Delay in microseconds
 */
void tiku_cpu_ambiq_delay_us(unsigned int us) {
    uint32_t reload = (SYST_RVR & SYST_MASK) + 1u;
    uint32_t per_us = (uint32_t)(tiku_cpu_ambiq_clock_get_hz() / 1000000UL);
    uint32_t last, now, step;
    uint64_t need;

    if (per_us == 0u) {
        per_us = 96u;
    }
    need = (uint64_t)us * per_us;

    if (reload <= 1u) {
        /* SysTick not configured yet (pre clock-init) — rough NOP fallback. */
        volatile uint32_t spin = (uint32_t)us * (per_us / 4u + 1u);
        while (spin--) {
            __asm__ volatile ("nop");
        }
        return;
    }

    last = SYST_CVR & SYST_MASK;
    while (need != 0u) {
        now  = SYST_CVR & SYST_MASK;            /* counts down; wraps to reload-1 */
        step = (now <= last) ? (last - now) : (last + reload - now);
        if ((uint64_t)step >= need) {
            break;
        }
        need -= step;
        last  = now;
    }
}

/**
 * @brief Spin-delay for a given number of milliseconds
 *
 * Calls tiku_cpu_ambiq_delay_us(1000) in a loop. Not suitable for
 * long sleeps — use the kernel timer subsystem instead.
 *
 * @param ms  Delay in milliseconds
 */
void tiku_cpu_ambiq_delay_ms(unsigned int ms) {
    while (ms--) {
        tiku_cpu_ambiq_delay_us(1000u);
    }
}

/**
 * @brief Read the device unique ID into a caller-provided buffer
 *
 * Reads the per-die unique chip ID from MCUCTRL CHIPID0/CHIPID1 (8 bytes
 * total) and packs the requested count, little-endian. Buffers shorter than
 * 8 bytes get a prefix; requests longer than 8 cap at 8.
 *
 * @param buf  Destination buffer for the unique ID
 * @param len  Number of bytes to fill (must be > 0; buf must be non-NULL)
 * @return Number of bytes written (<= 8), or 0 if buf is NULL or len is 0
 */
uint8_t tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len) {
    uint32_t id[2];
    uint8_t i, n;
    if (buf == 0 || len == 0) {
        return 0;
    }
    id[0] = MCUCTRL->CHIPID0;
    id[1] = MCUCTRL->CHIPID1;
    n = (len > 8u) ? 8u : len;
    for (i = 0; i < n; i++) {
        buf[i] = (uint8_t)(id[i >> 2] >> ((i & 3u) * 8u));
    }
    return n;
}

/**
 * @brief Return the encoded reason for the last system reset.
 *
 * Reads the Apollo reset generator's status latch (RSTGEN->STAT) and
 * maps it to an MSP430-SYSRSTIV-compatible code, which is the contract
 * the /sys reset consumers expect (tiku_vfs_tree_boot.c buckets it into
 * watchdog / power / reboot / other).  Multiple causes can latch across
 * a chain of resets; the most specific / most recent is reported first.
 *
 * Read-only: the STAT latch is left intact (the boot path samples it
 * once), so this never perturbs the reset generator.
 *
 * @return SYSRSTIV-compatible reset-reason code (0 = clean power-on).
 */
uint16_t tiku_cpu_ambiq_reset_reason(void) {
    uint32_t s = RSTGEN->STAT;

    /* RSTGEN->STAT bit positions are stable across the Apollo4/5 families,
     * but the CMSIS name for bit 1 is not (PORSTAT on Apollo4, POASTAT on
     * Apollo5), so decode by numeric mask rather than by macro name. */
    if (s & (1UL << 6)) {               /* WDRSTAT  watchdog reset        */
        return 0x0016u;                 /*                 -> "watchdog"  */
    }
    if (s & (1UL << 3)) {               /* SWRSTAT  software reset        */
        return 0x0006u;                 /*                 -> "reboot"    */
    }
    if (s & (1UL << 0)) {               /* EXRSTAT  external RST pin      */
        return 0x0014u;                 /*                 -> "reboot"    */
    }
    if (s & ((1UL << 2) | (1UL << 7) | (1UL << 8) |
             (1UL << 9) | (1UL << 10))) { /* BORSTAT / brown-out variants */
        return 0x0002u;                 /*                 -> "power"     */
    }
    if (s & (1UL << 1)) {               /* PORSTAT/POASTAT power-on       */
        return 0x0000u;                 /*                 -> "power"     */
    }
    return 0u;                          /* nothing latched -> power-on    */
}
