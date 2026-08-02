/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - STM32N6 1-Wire contract.
 *
 * No backend on this port yet: the calls exist so the kernel links, and each
 * reports failure rather than pretending a transfer happened.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_ONEWIRE_ARCH_H_
#define TIKU_STM32N6_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/** @brief Claim the 1-Wire pin. @return TIKU_OW_OK, or an error */
int     tiku_onewire_arch_init(void);

/** @brief Release the pin. */
void    tiku_onewire_arch_close(void);

/** @brief Send a reset pulse. @return TIKU_OW_OK when a device answers */
int     tiku_onewire_arch_reset(void);

/** @brief Send one bit. @param bit  Value */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/** @brief Read one bit. @return The bit */
uint8_t tiku_onewire_arch_read_bit(void);

/** @brief Send one byte. @param byte  Value */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/** @brief Read one byte. @return The byte */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_STM32N6_ONEWIRE_ARCH_H_ */
