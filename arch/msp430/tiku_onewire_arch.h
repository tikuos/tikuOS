/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.h - 1-Wire bus driver for MSP430 (architecture layer)
 *
 * Declares the architecture-specific 1-Wire functions implemented by
 * tiku_onewire_arch.c using GPIO bit-banging. These are called by the
 * platform-independent layer (interfaces/onewire/tiku_onewire.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ONEWIRE_ARCH_H_
#define TIKU_ONEWIRE_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <interfaces/onewire/tiku_onewire.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific 1-Wire initialization.
 *
 * Leaves the board's pin an input with its latch clear, so the external 4.7
 * kohm pull-up holds the line high and driving low later is a direction flip --
 * open drain.  Timings assume an 8 MHz MCLK, so the bus is reliable only there.
 *
 * @return TIKU_OW_OK (this port cannot fail)
 */
int tiku_onewire_arch_init(void);

/**
 * @brief Architecture-specific 1-Wire shutdown.
 *
 * Releases the pin by switching it back to input; the external
 * pull-up then holds the line high.
 */
void tiku_onewire_arch_close(void);

/**
 * @brief Issue a 1-Wire reset pulse and sample for a presence pulse.
 *
 * Timing-critical, so interrupts are masked for the whole ~960 us sequence.
 * The line is checked high after release, proving the pull-up, then sampled for
 * a slave pulling it low.
 *
 * @return TIKU_OW_OK if a presence pulse was seen,
 *         TIKU_OW_ERR_NO_DEVICE if no device responded or the line
 *         never returned high (missing pull-up / stuck bus)
 */
int tiku_onewire_arch_reset(void);

/**
 * @brief Write one bit into a 1-Wire time slot.
 *
 * Timing-critical, so interrupts are masked for the slot.  A write-1 is 2 us
 * low then 62 released; a write-0 is 60 low then 4.  Slaves sample around 30 us
 * in, which the short write-1 pulse is sized for.
 *
 * @param bit  Value to write; only the least significant bit is used
 */
void tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Read one bit from a 1-Wire time slot.
 *
 * Timing-critical, so interrupts are masked.  Drive low 2 us, release, wait 10
 * and sample -- inside the 15 us the master has -- then idle 50.  That settle
 * also covers the input synchroniser's two-cycle lag after a direction change.
 *
 * @return The sampled bit (0 or 1)
 */
uint8_t tiku_onewire_arch_read_bit(void);

/**
 * @brief Write one byte to the 1-Wire bus, least significant bit first.
 *
 * Issues eight write slots via tiku_onewire_arch_write_bit(); takes
 * roughly 512 us.  Interrupts are disabled per slot rather than for
 * the whole byte, so an ISR can run between bits.
 *
 * @param byte  Value to write
 */
void tiku_onewire_arch_write_byte(uint8_t byte);

/**
 * @brief Read one byte from the 1-Wire bus, least significant bit first.
 *
 * Issues eight read slots via tiku_onewire_arch_read_bit(); takes
 * roughly 500 us.  Interrupts are disabled per slot rather than for
 * the whole byte, so an ISR can run between bits.
 *
 * @return The assembled byte
 */
uint8_t tiku_onewire_arch_read_byte(void);

#endif /* TIKU_ONEWIRE_ARCH_H_ */
