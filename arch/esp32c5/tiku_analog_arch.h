/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_analog_arch.h - exclusive C5 SAR ownership and its analog registers.
 *
 * One owner at a time among the entropy source, the ADC and the radio front
 * end.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_ANALOG_ARCH_H_
#define TIKU_ESP32C5_ANALOG_ARCH_H_
#include <stdint.h>

enum {
    TIKU_C5_ANALOG_NONE,
    TIKU_C5_ANALOG_ENTROPY,
    TIKU_C5_ANALOG_PHY,
    TIKU_C5_ANALOG_ADC
};

/** @brief Claim the analog subsystem for a named owner; return -1 if busy or
 * invalid. */
int tiku_c5_analog_acquire(unsigned owner);
/** @brief Release a matching claim; return -1 without changing another owner's
 * claim. */
int tiku_c5_analog_release(unsigned owner);
/** @brief Return the owner, or TIKU_C5_ANALOG_NONE when unclaimed. */
unsigned tiku_c5_analog_owner(void);
/** @brief Read (@p write 0) or write one SAR analog register over the
 * internal bus, whose clocks the caller has enabled.
 * @return 0, or -1 if the bus stays busy */
int tiku_c5_analog_sar_reg(uint8_t reg, uint8_t *value, int write);
#endif
