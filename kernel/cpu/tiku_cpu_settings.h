/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_settings.h - next-boot CPU clock preference.
 *
 * Saves a choice among the clock rates the port advertises and applies it at
 * the next boot.  Nordic builds none of these functions: its preference is in
 * arch/nordic/tiku_cpu_settings_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CPU_SETTINGS_H_
#define TIKU_CPU_SETTINGS_H_

/**
 * @brief The clock rate the saved preference asks for.
 * @return The saved rate when it is valid and advertised, otherwise the rate
 *         this boot started at
 */
unsigned long tiku_cpu_settings_target(void);
/**
 * @brief Save an advertised Hz choice for the next boot.
 *
 * The running clock is not changed.
 *
 * @param hz  A rate tiku_cpu_freq_available() lists
 * @return 0 when the choice is saved and verified, -1 otherwise
 * @note Returns -1 before tiku_cpu_settings_boot() has run.
 */
int tiku_cpu_settings_save(unsigned long hz);
/**
 * @brief Apply the saved clock choice, if any, at boot.
 * @note Call once, before peripherals are initialised; later calls do
 *       nothing.
 */
void tiku_cpu_settings_boot(void);

#endif
