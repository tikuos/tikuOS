/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_analog_arch.h - exclusive C5 SAR entropy and radio analog ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_ANALOG_ARCH_H_
#define TIKU_ESP32C5_ANALOG_ARCH_H_
#include <stdint.h>

enum { TIKU_C5_ANALOG_NONE, TIKU_C5_ANALOG_ENTROPY, TIKU_C5_ANALOG_PHY };

/** @brief Claim the analog subsystem for a named owner; return -1 if busy or
 * invalid. */
int tiku_c5_analog_acquire(unsigned owner);
/** @brief Release a matching claim; return -1 without changing another owner's
 * claim. */
int tiku_c5_analog_release(unsigned owner);
/** @brief Return the owner, or TIKU_C5_ANALOG_NONE when unclaimed. */
unsigned tiku_c5_analog_owner(void);
#endif
