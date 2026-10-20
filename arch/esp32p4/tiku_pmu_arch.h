/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.h - P4 v1.3 calibrated power and 360 MHz CPLL startup.
 *
 * Both PLL calls run with the CPU on the 40 MHz crystal and leave it there;
 * the 320 MHz MPLL clocks the 80 MHz PSRAM bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_PMU_ARCH_H_
#define TIKU_ESP32P4_PMU_ARCH_H_
/**
 * @brief Initialize calibrated regulators and the 360 MHz CPLL, leaving XTAL selected.
 * @note Requires CPU0 at 40 MHz in SRAM, IRQs masked and other cores parked.
 * @return Zero on success; -1 on invalid clock/ROM, -2 on I2C/readback error,
 * or -3 on calibration timeout. Errors leave the CPU root on XTAL.
 */
int tiku_p4_pmu_cpll_init(void);
/** @brief Initialize the v1.3 MPLL at 320 MHz for an 80 MHz PSRAM bus.
 * @note Requires CPU0 at 40 MHz, masked IRQs and no active MPLL consumers.
 * @return Zero on success, negative on identity, readback or calibration failure.
 */
int tiku_p4_pmu_mpll_init(void);
/** @brief Power the PSRAM's 1.8 V supply, LDO VO2, at its eFuse calibration.
 * @note Requires CPU0 at 40 MHz; returns after the 200 us settling time.
 * @return Zero once the LDO reads back enabled at the setting, -2 otherwise.
 */
int tiku_p4_pmu_psram_ldo_init(void);
/** @brief The brownout level ESP-IDF selects by default: about 2.6 V. */
#define TIKU_P4_BROWNOUT_LEVEL 7u
/**
 * @brief Reset the whole system when the supply falls below about 2.6 V.
 *
 * Mode 0 of the detector at TIKU_P4_BROWNOUT_LEVEL resets after its wait,
 * with the flash cut off first; mode 1, the fixed analog reset, is turned
 * off as ESP-IDF turns it off.  The setting is lost at that reset.
 *
 * @return Zero, or -2 when the analog bus did not take the level
 */
int tiku_p4_brownout_init(void);
/** @brief The level mode 0 resets the system at, or -1 when it does not. */
int tiku_p4_brownout_armed_level(void);
#endif
