/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.h - C5 core voltage and PLL, set before the CPU clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C5_PMU_ARCH_H_
#define TIKU_ESP32C5_PMU_ARCH_H_

#include <stdint.h>

/** Regulator setting when eFuse holds no calibration (ESP-IDF default). */
#define TIKU_C5_PMU_DBIAS_DEFAULT 28u

/**
 * @brief Hand the digital regulators to the PMU at the calibrated voltage.
 *
 * Sets the eFuse calibration plus 19 (at most 31), or 28 without one, as
 * ESP-IDF's bootloader does; only the first call writes the registers.
 *
 * @return 0 when the regulators hold the calibrated setting, -1 when the
 *         analog bus did not respond (the setting is then unchanged).
 * @note   Masks interrupts for the analog-bus transfers.
 */
int tiku_c5_core_voltage_ready(void);

/** @brief The HP active regulator setting applied, or 0 before success. */
uint32_t tiku_c5_core_voltage_dbias(void);

/**
 * @brief Power the 480 MHz PLL up and calibrate it from the crystal, as
 *        ESP-IDF's bootloader does; the first call does the work.
 *
 * @return 0 when the PLL is calibrated, -1 when the crystal is not 48 or
 *         40 MHz, the analog bus stays busy or calibration does not finish
 *         (the PLL is then powered down again).
 * @note   Masks interrupts for the analog-bus transfers.
 */
int tiku_c5_pll_ready(void);

/** @brief 1 before tiku_c5_pll_ready() ran, then its result. */
int tiku_c5_pll_result(void);

/**
 * @brief Hand the regulator to the PVT monitor, which tracks the core
 *        voltage with process, temperature and load, as ESP-IDF does once
 *        the CPU runs from the PLL; the first call does the work.
 *
 * @return 0 when tracking runs, 2 when the eFuse block carries no PVT
 *         calibration and the static setting stays.
 */
int tiku_c5_pvt_ready(void);

/** @brief 1 before tiku_c5_pvt_ready() ran, then its result. */
int tiku_c5_pvt_result(void);

#endif /* TIKU_ESP32C5_PMU_ARCH_H_ */
