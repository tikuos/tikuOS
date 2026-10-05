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
 * the next boot.  Nordic has its own, in arch/nordic/tiku_cpu_settings_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CPU_SETTINGS_H_
#define TIKU_CPU_SETTINGS_H_

/** @brief Read a validated target, falling back to this boot's default. */
unsigned long tiku_cpu_settings_target(void);
/**
 * @brief Save an advertised Hz choice for the next boot.
 *
 * The running clock is not changed.
 *
 * @return 0 when the choice is saved and verified, -1 otherwise
 */
int tiku_cpu_settings_save(unsigned long hz);
/**
 * @brief Apply the saved clock choice, if any, at boot.
 * @note Call once, before peripherals are initialised; later calls do
 *       nothing.
 */
void tiku_cpu_settings_boot(void);

#endif
