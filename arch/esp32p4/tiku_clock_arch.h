/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_arch.h - P4 CPU clock control from internal SRAM.
 *
 * The CPU runs from the 40 MHz crystal or from the 360 MHz CPLL at 90, 180
 * or 360 MHz; a PLL rate needs tiku_p4_clock_pll_init() first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_CLOCK_ARCH_H_
#define TIKU_ESP32P4_CLOCK_ARCH_H_
/**
 * @brief Select 40 MHz crystal clocks for CPU, memory, system and APB.
 * @note CPU0 SRAM startup only, with IRQs masked and other cores parked.
 * @return Zero on clock-update completion; -1 on bounded timeout.
 */
int tiku_p4_clock_xtal_init(void);
/** @brief Initialize the native 360 MHz CPLL; leave the CPU at 40 MHz.
 * @note Requires masked IRQs, CPU0 in SRAM, and other cores parked.
 * @return Zero on success, negative on power, calibration or clock failure.
 */
int tiku_p4_clock_pll_init(void);
/** @brief Select 40, 90, 180 or 360 MHz; return -1 for unsupported/unready
 * requests, -2 on divider failure. A divider failure selects the XTAL root.
 * @note SRAM diagnostic only; no active DMA or clock-dependent peripherals.
 */
int tiku_p4_clock_set_mhz(unsigned mhz);
#endif
