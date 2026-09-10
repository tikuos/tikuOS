/* Persistent boot-time clock policy. SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CPU_SETTINGS_H_
#define TIKU_CPU_SETTINGS_H_

/** @brief Read a validated target, falling back to this boot's default. */
unsigned long tiku_cpu_settings_target(void);
/** @brief Persist an advertised Hz choice; never change hardware clocks. */
int tiku_cpu_settings_save(unsigned long hz);
/** @brief Consume restored settings once before peripheral initialization. */
void tiku_cpu_settings_boot(void);

#endif
