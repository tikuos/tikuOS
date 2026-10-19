/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_arch.h - watchdog handoff from the C5 ROM loader.
 *
 * Turns off the timer-group and LP watchdogs and sets the super watchdog to
 * feed itself.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_BOOT_ARCH_H_
#define TIKU_ESP32C5_BOOT_ARCH_H_

/** @brief Disable boot watchdogs and enable the super-watchdog's auto-feed. */
void tiku_esp32c5_boot_watchdogs_disable(void);

#endif
