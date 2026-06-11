/*
 * Tiku Operating System v0.05
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
 * never disagree. Scaled by the SysTick clock (= TIKU_MAIN_CPU_HZ, 48 MHz =
 * core/2 on this M55 — NOT the 96 MHz core). No AmbiqSuite dependency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku.h"              /* TIKU_MAIN_CPU_HZ = 48 MHz SysTick clock */
#include "tiku_cpu_common.h"

/* Cortex-M SysTick (System Control Space) — 24-bit down-counter, auto-reload. */
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
#define SYST_MASK 0x00FFFFFFu

void tiku_cpu_ambiq_delay_us(unsigned int us) {
    uint32_t reload = (SYST_RVR & SYST_MASK) + 1u;
    uint32_t per_us = (uint32_t)(TIKU_MAIN_CPU_HZ / 1000000UL);  /* SysTick clock (48) */
    uint32_t last, now, step;
    uint64_t need;

    if (per_us == 0u) {
        per_us = 48u;
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

void tiku_cpu_ambiq_delay_ms(unsigned int ms) {
    while (ms--) {
        tiku_cpu_ambiq_delay_us(1000u);
    }
}

uint8_t tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len) {
    /* TODO: read the device unique ID from MCUCTRL/OTP. */
    uint8_t i;
    if (buf == 0 || len == 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        buf[i] = 0u;
    }
    return len;
}

uint16_t tiku_cpu_ambiq_reset_reason(void) {
    /* TODO: decode RSTGEN->STAT into the tikuOS reset-reason bits. */
    return 0u;
}
