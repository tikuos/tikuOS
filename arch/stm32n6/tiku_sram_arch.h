/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sram_arch.h - STM32N6 internal SRAM banks.
 *
 * The boot ROM leaves most of the 3.75 MB array clock-gated and held in reset;
 * the image window it loads into is a small part of what the part has.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_SRAM_ARCH_H_
#define TIKU_STM32N6_SRAM_ARCH_H_

#include <stdint.h>

/* The AXI SRAM array is one contiguous span; the image window the boot ROM
 * loads into sits inside it, which is why the arena is described as the two
 * pieces either side rather than as one region. */
#define TIKU_STM32N6_SRAM_BASE      0x34000000UL
#define TIKU_STM32N6_SRAM_END       0x343C0000UL

/* Below the ROM's download buffer: the ROM keeps its context and traces in the
 * first 24 KB of AXISRAM2, so the reclaimable part starts above them. */
#define TIKU_STM32N6_SRAM_LOW_BASE  0x34000000UL
#define TIKU_STM32N6_SRAM_LOW_END   0x34180000UL
#define TIKU_STM32N6_ROM_KEEP_BASE  0x34100000UL
#define TIKU_STM32N6_ROM_KEEP_END   0x34106000UL

/* Above the image window (__stack), up to the top of the backed array.  The
 * address range runs on to 0x34400000, but an access past 0x343C0000 HANGS
 * the bus rather than faulting -- no fault dump, no reset. */
#define TIKU_STM32N6_SRAM_HIGH_END  0x343C0000UL

/**
 * @brief Clock and un-reset every internal SRAM bank.
 *
 * Runs before the memory subsystem so the banks answer by the time the region
 * table describes them. Idempotent.
 */
void tiku_stm32n6_sram_init(void);

/**
 * @brief Report the banks the last init call left enabled.
 *
 * @return RCC_MEMENR as read back after the enable
 */
uint32_t tiku_stm32n6_sram_enabled_mask(void);

#if defined(TIKU_N6_SRAM_PROBE)
/**
 * @brief Walk every bank writing and re-reading a unique word per 64 KB.
 *
 * Destructive.  Each page's result character is drained before the next
 * access, so the page that bus-faults is the first one with no character.
 */
void tiku_stm32n6_sram_probe(void);
#endif

#endif /* TIKU_STM32N6_SRAM_ARCH_H_ */
