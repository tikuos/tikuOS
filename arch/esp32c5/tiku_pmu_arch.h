/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pmu_arch.h - C5 digital core voltage for the CPU clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C5_PMU_ARCH_H_
#define TIKU_ESP32C5_PMU_ARCH_H_

#include <stdint.h>

/** Regulator setting when eFuse holds no calibration (ESP-IDF default). */
#define TIKU_C5_PMU_DBIAS_DEFAULT   28u

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

#endif /* TIKU_ESP32C5_PMU_ARCH_H_ */
