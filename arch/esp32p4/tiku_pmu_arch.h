/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_pmu_arch.h - P4 v1.3 calibrated power and 360 MHz CPLL startup.
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
#endif
