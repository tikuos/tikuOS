/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - nRF54L 1-Wire arch header (stub port).
 *
 * Declares the 1-Wire backend the 1-Wire interface calls.  This port has no
 * 1-Wire driver: every function is a stub.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_ONEWIRE_ARCH_H_
#define TIKU_NORDIC_ONEWIRE_ARCH_H_

#include <stdint.h>
#include <interfaces/onewire/tiku_onewire.h>

/**
 * @brief Configures nothing.
 *
 * @return -1 (TIKU_OW_ERR_NO_DEVICE), always
 */
int     tiku_onewire_arch_init(void);

/**
 * @brief Does nothing.
 */
void    tiku_onewire_arch_close(void);

/**
 * @brief Sends no reset pulse.
 *
 * @return -1 (TIKU_OW_ERR_NO_DEVICE), always
 */
int     tiku_onewire_arch_reset(void);

/**
 * @brief Writes nothing.
 *
 * @param bit  Ignored
 */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Samples nothing.
 *
 * @return 1, the level of an idle pulled-up line, always
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Writes nothing.
 *
 * @param byte  Ignored
 */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Samples nothing.
 *
 * @return 0xFF, eight idle-high bits, always
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_NORDIC_ONEWIRE_ARCH_H_ */
