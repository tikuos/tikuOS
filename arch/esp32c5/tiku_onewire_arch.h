/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - C5 1-Wire master, open drain on TIKU_BOARD_OW_PIN.
 *
 * Slots are timed in CPU cycles; a bus with devices needs an external
 * 4.7 kOhm pull-up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_ONEWIRE_ARCH_H_
#define TIKU_ESP32C5_ONEWIRE_ARCH_H_
#include <interfaces/onewire/tiku_onewire.h>

/** @brief Make the pin an open-drain GPIO with its pull-up, bus released.
 * The internal pull-up holds an unwired bus high; a bus with devices still
 * needs the external 4.7 kOhm.  @return TIKU_OW_OK, or PARAM for a bad pin
 */
int     tiku_onewire_arch_init(void);
/** @brief Restore the pin's previous configuration. */
void    tiku_onewire_arch_close(void);
/** @brief 480 us reset pulse, then sample for a presence pulse at 70 us.
 * @return TIKU_OW_OK if a device pulled the bus low, else ERR_NO_DEVICE
 */
int     tiku_onewire_arch_reset(void);
/** @brief One 70 us write slot: low 6 us for a 1, 60 us for a 0. */
void    tiku_onewire_arch_write_bit(uint8_t bit);
/** @brief One read slot: low 6 us, sample at 15 us. @return The bus level */
uint8_t tiku_onewire_arch_read_bit(void);
/** @brief Eight write slots, least significant bit first. */
void    tiku_onewire_arch_write_byte(uint8_t byte);
/** @brief Eight read slots, least significant bit first. */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_ESP32C5_ONEWIRE_ARCH_H_ */
