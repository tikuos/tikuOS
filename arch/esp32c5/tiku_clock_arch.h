/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_clock_arch.h - C5 CPU divider control, and the crystal for a sleep.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_CLOCK_ARCH_H_
#define TIKU_ESP32C5_CLOCK_ARCH_H_

#include <stdint.h>

/** @brief Apply 40, 80 or 240 MHz, bringing the PLL up from the crystal when
 *         the ROM left the CPU on it; return -1 on refusal. */
int tiku_c5_clock_set(unsigned long hz);
/** @brief Return nonzero after an apply or rollback hardware failure. */
int tiku_c5_clock_fault(void);
/**
 * @brief Move the CPU and AHB onto the crystal, undivided, for a sleep that
 *        stops the PLL; interrupts must be masked.
 * @return The tree to give back to tiku_c5_clock_unpark().
 */
uint32_t tiku_c5_clock_park(void);
/** @brief Restore the clock tree tiku_c5_clock_park() returned. */
void tiku_c5_clock_unpark(uint32_t saved);
#endif
