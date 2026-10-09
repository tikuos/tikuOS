/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_clock_arch.h - C5 boot-only CPU divider control.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_CLOCK_ARCH_H_
#define TIKU_ESP32C5_CLOCK_ARCH_H_

/** @brief Apply 40, 80 or 240 MHz, bringing the PLL up from the crystal when
 *         the ROM left the CPU on it; return -1 on refusal. */
int tiku_c5_clock_set(unsigned long hz);
/** @brief Return nonzero after an apply or rollback hardware failure. */
int tiku_c5_clock_fault(void);
#endif
