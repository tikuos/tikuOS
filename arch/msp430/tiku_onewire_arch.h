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
 * Configures the board's 1-Wire pin (TIKU_BOARD_OW_* macros) as a
 * GPIO input with the output latch cleared, so the line is released
 * and held high by the external pull-up.  Driving low later is done
 * by flipping the direction bit, giving open-drain behaviour.
 *
 * An external 4.7 kohm pull-up on the data line is required.  All
 * bit timings assume an 8 MHz MCLK (they are generated with
 * __delay_cycles()), so the bus is only reliable at that clock.
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
 * Timing-critical: interrupts (GIE) are disabled for the whole
 * ~960 us sequence.  The line is driven low for 480 us and released;
 * 15 us later the line is sampled to confirm the pull-up brought it
 * high, then again at ~70 us after release to catch a slave pulling
 * it low (the presence pulse).  A further 410 us completes the reset
 * window before interrupts are restored.
 *
 * @return TIKU_OW_OK if a presence pulse was seen,
 *         TIKU_OW_ERR_NO_DEVICE if no device responded or the line
 *         never returned high (missing pull-up / stuck bus)
 */
int tiku_onewire_arch_reset(void);

/**
 * @brief Write one bit into a 1-Wire time slot.
 *
 * Timing-critical: interrupts (GIE) are disabled for the duration of
 * the slot.  A write-1 drives the line low for 2 us then releases it
 * for 62 us; a write-0 drives low for 60 us then releases for 4 us.
 * Slaves sample around 30 us into the slot, so the short write-1
 * pulse guarantees the line is high by then.
 *
 * @param bit  Value to write; only the least significant bit is used
 */
void tiku_onewire_arch_write_bit(uint8_t bit);

/**
 * @brief Read one bit from a 1-Wire time slot.
 *
 * Timing-critical: interrupts (GIE) are disabled for the duration of
 * the slot.  The master drives the line low for 2 us, releases it,
 * waits 10 us and samples (the master must sample within 15 us of
 * the falling edge), then idles 50 us for a ~62 us slot.  The 10 us
 * settle also covers the MSP430 input synchronizer: PxIN is clocked
 * through a flip-flop on MCLK and can lag a direction change by up
 * to two MCLK cycles.
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
