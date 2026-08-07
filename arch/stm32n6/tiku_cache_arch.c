/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cache_arch.c - Cortex-M55 cache control for the STM32N6.
 *
 * Geometry is read from CCSIDR rather than assumed, and the set/way loops
 * derive their shifts from it, so a different cache build still walks cleanly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cache_arch.h"
#include "tiku_stm32n6_regs.h"

/**
 * @brief Walk every set and way of the L1 data cache with one maintenance op.
 *
 * @param op  STM32N6_SCB_DCISW to drop lines, STM32N6_SCB_DCCISW to clean too
 */
static void dcache_all(uint32_t op) {
    /* Select the L1 data cache before its geometry can be read. */
    TIKU_REG32(STM32N6_SCB_CSSELR) = 0UL;
    __asm__ volatile ("dsb" ::: "memory");

    uint32_t ccsidr = TIKU_REG32(STM32N6_SCB_CCSIDR);
    uint32_t sets   = (ccsidr >> 13) & 0x7FFFUL;
    uint32_t ways   = (ccsidr >> 3)  & 0x3FFUL;

    /* Ways pack against bit 31, so their shift is however many leading zero
     * bits the way count leaves free; sets sit just above the line offset. */
    uint32_t wshift = (uint32_t)__builtin_clz((ways == 0UL) ? 1UL : ways);

    for (uint32_t s = 0UL; s <= sets; s++) {
        for (uint32_t w = 0UL; w <= ways; w++) {
            TIKU_REG32(op) = (w << wshift) | (s << 5);
        }
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

void tiku_stm32n6_cache_enable(void) {
    uint32_t ccr = TIKU_REG32(STM32N6_SCB_CCR);

    /* Both caches hold junk after power-up, so each is invalidated before its
     * enable bit lands; an already-on cache is left exactly as it is. */
    if ((ccr & STM32N6_SCB_CCR_IC) == 0UL) {
        TIKU_REG32(STM32N6_SCB_ICIALLU) = 0UL;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
        TIKU_REG32(STM32N6_SCB_CCR) |= STM32N6_SCB_CCR_IC;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
    }

    if ((TIKU_REG32(STM32N6_SCB_CCR) & STM32N6_SCB_CCR_DC) == 0UL) {
        dcache_all(STM32N6_SCB_DCISW);
        TIKU_REG32(STM32N6_SCB_CCR) |= STM32N6_SCB_CCR_DC;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
    }
}

void tiku_stm32n6_cache_disable(void) {
    if (TIKU_REG32(STM32N6_SCB_CCR) & STM32N6_SCB_CCR_DC) {
        /* Order matters: cleaning while the cache is still enabled loses any
         * line dirtied between the clean and the disable, so DC goes off
         * first. */
        TIKU_REG32(STM32N6_SCB_CCR) &= ~STM32N6_SCB_CCR_DC;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
        dcache_all(STM32N6_SCB_DCCISW);
    }
    if (TIKU_REG32(STM32N6_SCB_CCR) & STM32N6_SCB_CCR_IC) {
        TIKU_REG32(STM32N6_SCB_CCR) &= ~STM32N6_SCB_CCR_IC;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
        TIKU_REG32(STM32N6_SCB_ICIALLU) = 0UL;
        __asm__ volatile ("dsb\n\tisb" ::: "memory");
    }
}

uint32_t tiku_stm32n6_cache_state(void) {
    uint32_t ccr = TIKU_REG32(STM32N6_SCB_CCR);
    return ((ccr & STM32N6_SCB_CCR_IC) ? 1UL : 0UL) |
           ((ccr & STM32N6_SCB_CCR_DC) ? 2UL : 0UL);
}

/**
 * @brief Shared walker for the by-address maintenance operations.
 *
 * @param op    Register to write per line
 * @param addr  Range start
 * @param len   Range length in bytes
 */
static void dcache_range(uint32_t op, const void *addr, size_t len) {
    if ((TIKU_REG32(STM32N6_SCB_CCR) & STM32N6_SCB_CCR_DC) == 0UL) {
        return;                                  /* nothing cached to maintain */
    }
    uintptr_t p   = (uintptr_t)addr & ~(STM32N6_CACHE_LINE - 1UL);
    uintptr_t end = (uintptr_t)addr + len;

    __asm__ volatile ("dsb" ::: "memory");
    while (p < end) {
        TIKU_REG32(op) = (uint32_t)p;
        p += STM32N6_CACHE_LINE;
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

void tiku_stm32n6_dcache_clean(const void *addr, size_t len) {
    dcache_range(STM32N6_SCB_DCCMVAC, addr, len);
}

void tiku_stm32n6_dcache_invalidate(const void *addr, size_t len) {
    dcache_range(STM32N6_SCB_DCIMVAC, addr, len);
}
