/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_arch.h - watchdog handoff from the P4 ROM loader.
 *
 * Declares the early-boot calls that stop the watchdogs a ROM boot can
 * leave running and park CPU1 and, without its payload, the LP core.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_BOOT_ARCH_H_
#define TIKU_ESP32P4_BOOT_ARCH_H_

/** @brief Disable boot watchdogs and enable the super-watchdog's auto-feed. */
void tiku_esp32p4_boot_watchdogs_disable(void);

/** @brief Hold CPU1, and the LP core in an image without its payload, in
 *         reset with their clocks disabled. */
void tiku_esp32p4_boot_park_cores(void);

#endif
