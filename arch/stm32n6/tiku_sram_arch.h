/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sram_arch.h - STM32N6 internal SRAM banks.
 *
 * The boot ROM leaves most of the 3.75 MB AXI SRAM array clock-gated and held
 * in reset; tiku_stm32n6_sram_init() enables every bank.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_SRAM_ARCH_H_
#define TIKU_STM32N6_SRAM_ARCH_H_

#include <stdint.h>

/*
 * AXI SRAM address map.  The array is one contiguous span, and the image
 * window the boot ROM loads into (0x34180400..0x341C0000) sits inside it:
 *
 *   SRAM_BASE..SRAM_END          the whole backed array
 *   SRAM_LOW_BASE..SRAM_LOW_END  below the image window
 *   ROM_KEEP_BASE..ROM_KEEP_END  the first 24 KB of AXISRAM2, where the ROM
 *                                keeps its context and traces
 *   __stack..SRAM_HIGH_END       above the image window
 *
 * Addresses run on to 0x34400000, but an access past 0x343C0000 hangs the
 * bus with no fault and no reset.
 */
#define TIKU_STM32N6_SRAM_BASE      0x34000000UL
#define TIKU_STM32N6_SRAM_END       0x343C0000UL
#define TIKU_STM32N6_SRAM_LOW_BASE  0x34000000UL
#define TIKU_STM32N6_SRAM_LOW_END   0x34180000UL
#define TIKU_STM32N6_ROM_KEEP_BASE  0x34100000UL
#define TIKU_STM32N6_ROM_KEEP_END   0x34106000UL
#define TIKU_STM32N6_SRAM_HIGH_END  0x343C0000UL

/**
 * @brief Clock every internal SRAM bank, release its reset and lift AXISRAM3..6
 *        out of shutdown.
 *
 * Repeat calls leave the banks as they are.
 *
 * @note Call before anything touches .axisram or the tier span.
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
 * access, so a page that hangs or faults the bus is the first one with no
 * character.
 */
void tiku_stm32n6_sram_probe(void);
#endif

#endif /* TIKU_STM32N6_SRAM_ARCH_H_ */
