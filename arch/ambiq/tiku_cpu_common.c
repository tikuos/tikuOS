/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - Apollo510 CPU helpers: delays, chip ID, reset reason.
 *
 * Delays count the free-running SysTick (CLKSOURCE = processor) and scale by
 * the core clock that tiku_cpu_ambiq_clock_get_hz() reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "hal/tiku_cpu.h"
#include "tiku.h"              /* platform configuration */
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"  /* tiku_cpu_ambiq_clock_get_hz */
#include "apollo510.h"         /* CMSIS register map: MCUCTRL, RSTGEN */

/**
 * @defgroup SYST_REGS SysTick register accessors
 * @brief Cortex-M SysTick: a 24-bit auto-reload down-counter at the core
 *        clock, the delay timebase.
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
 * Counts SysTick cycles (CLKSOURCE = processor), scaled by the core clock
 * read at entry, so a delay after an LP/HP perf-mode switch uses the new
 * rate.  Before SysTick is configured it spins an uncalibrated NOP loop.
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
        /* SysTick not configured yet (before clock init): rough NOP loop. */
        volatile uint32_t spin = (uint32_t)us * (per_us / 4u + 1u);
        while (spin--) {
            __asm__ volatile ("nop");
        }
        return;
    }

    last = SYST_CVR & SYST_MASK;
    while (need != 0u) {
        now  = SYST_CVR & SYST_MASK;            /* counts down; wraps to RVR */
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
 * Calls tiku_cpu_ambiq_delay_us(1000) @p ms times, keeping the core busy
 * throughout.
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
 * @param len  Number of bytes to fill
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
 * Maps the reset generator's status latch (RSTGEN->STAT) to the
 * MSP430-SYSRSTIV-style code the /sys reset nodes decode.  Several causes can
 * latch at once; the most specific is reported.  STAT is not cleared.
 *
 * @return SYSRSTIV-style reset-reason code (0 = power-on)
 */
uint16_t tiku_cpu_ambiq_reset_reason(void) {
    uint32_t s = RSTGEN->STAT;

    /* Bits are decoded by number: their positions match on Apollo4 and
     * Apollo5, but the CMSIS name of bit 1 differs (PORSTAT on Apollo4,
     * POASTAT on Apollo5). */
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

static uint32_t clock_users[TIKU_AMBIQ_CLOCK_USERS];
static uint32_t clock_baseline;
static uint8_t clock_seeded;

void tiku_ambiq_clock_force(tiku_ambiq_clock_owner_t owner, uint32_t mask)
{
    const uint32_t bits = CLKGEN_MISC_FRCHFRC_Msk |
                          CLKGEN_MISC_FRCHFRC2_Msk;
    uint32_t force;

    if ((unsigned)owner >= TIKU_AMBIQ_CLOCK_USERS) {
        return;
    }
    tiku_atomic_enter();
    if (!clock_seeded) {
        clock_baseline = CLKGEN->MISC & bits;
        clock_seeded = 1u;
    }
    clock_users[owner] = mask & bits;
    force = clock_baseline;
    for (unsigned i = 0; i < TIKU_AMBIQ_CLOCK_USERS; ++i) {
        force |= clock_users[i];
    }
    CLKGEN->MISC = (CLKGEN->MISC & ~bits) | force;
    __DSB();
    tiku_atomic_exit();
}
