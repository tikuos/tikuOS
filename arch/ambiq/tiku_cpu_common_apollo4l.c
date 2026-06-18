/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common_apollo4l.c - Apollo4 Lite common CPU helpers (delays, IDs)
 *
 * Mirrors arch/ambiq/tiku_cpu_common.c (Apollo510). Bare-metal delays spin on
 * the Cortex-M SysTick down-counter (core peripheral, identical on M4 and M55),
 * scaled by TIKU_MAIN_CPU_HZ. Falls back to a NOP spin when SysTick is not yet
 * configured (e.g. the minimal smoke build, which has no kernel tick).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "tiku.h"              /* TIKU_MAIN_CPU_HZ SysTick clock */
#include "tiku_cpu_common.h"
#include "apollo4l.h"          /* CMSIS register map (MCUCTRL CHIPID) -- register header only */

/** Reload Value Register (24-bit) */
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
/** Current Value Register (24-bit, counts down) */
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
/** Mask for the 24 valid counter bits */
#define SYST_MASK 0x00FFFFFFu

/**
 * @brief Spin-delay for a given number of microseconds.
 *
 * Uses the Cortex-M SysTick down-counter scaled by TIKU_MAIN_CPU_HZ. Falls
 * back to a NOP spin loop when SysTick is not yet configured.
 *
 * @param us  Delay in microseconds
 */
void tiku_cpu_ambiq_delay_us(unsigned int us) {
    uint32_t reload = (SYST_RVR & SYST_MASK) + 1u;
    uint32_t per_us = (uint32_t)(TIKU_MAIN_CPU_HZ / 1000000UL);
    uint32_t last, now, step;
    uint64_t need;

    if (per_us == 0u) {
        per_us = 96u;
    }
    need = (uint64_t)us * per_us;

    if (reload <= 1u) {
        /* SysTick not configured yet -- rough NOP fallback. */
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

/** @brief Spin-delay for a given number of milliseconds. */
void tiku_cpu_ambiq_delay_ms(unsigned int ms) {
    while (ms--) {
        tiku_cpu_ambiq_delay_us(1000u);
    }
}

/**
 * @brief Read the device unique ID from MCUCTRL CHIPID0/CHIPID1 (8 bytes).
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
 * @brief Return the encoded reset reason (stub -- always 0).
 *
 * @return 0 until RSTGEN->STAT decoding is implemented
 */
uint16_t tiku_cpu_ambiq_reset_reason(void) {
    return 0u;
}
