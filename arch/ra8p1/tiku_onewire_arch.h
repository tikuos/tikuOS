/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - RA8P1 1-Wire contract.
 *
 * This port has no 1-Wire driver: init and reset return errors, the writes
 * do nothing, and the reads return the idle-high bus pattern.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_ONEWIRE_ARCH_H_
#define TIKU_RA8P1_ONEWIRE_ARCH_H_

#include <interfaces/onewire/tiku_onewire.h>

/** @brief Claims no pin. @return TIKU_OW_ERR_PARAM */
int     tiku_onewire_arch_init(void);

/** @brief Does nothing. */
void    tiku_onewire_arch_close(void);

/** @brief Sends no pulse. @return TIKU_OW_ERR_NO_DEVICE */
int     tiku_onewire_arch_reset(void);

/** @brief Discards the bit. @param bit  Ignored */
void    tiku_onewire_arch_write_bit(uint8_t bit);

/** @brief Samples nothing. @return 1, the idle-high bus level */
uint8_t tiku_onewire_arch_read_bit(void);

/** @brief Discards the byte. @param byte  Ignored */
void    tiku_onewire_arch_write_byte(uint8_t byte);

/** @brief Samples nothing. @return 0xFF, the idle-high bus pattern */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_RA8P1_ONEWIRE_ARCH_H_ */
