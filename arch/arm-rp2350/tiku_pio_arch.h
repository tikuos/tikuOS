/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pio_arch.h - RP2350 PIO (programmable I/O) driver.
 *
 * One state machine on PIO0 runs a six-instruction program that shifts a data
 * word of up to 32 bits out to a pin and raises an IRQ when done.  One word
 * per call; there is no autopull.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_PIO_ARCH_H_
#define TIKU_RP2350_PIO_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_PIO_OK              0  /**< Operation succeeded */
#define TIKU_PIO_ERR_BUSY       -1  /**< A transmission is in progress */
#define TIKU_PIO_ERR_INVALID    -2  /**< Pin > 47, bit_count outside 1-32,
                                         or period outside divider range */
#define TIKU_PIO_ERR_NOT_READY  -3  /**< Driver not initialised, or abort
                                         with no transmission running */

/*---------------------------------------------------------------------------*/
/* CALLBACK                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Completion callback, called in PIO0_IRQ_0 ISR context.
 *
 * Called once the state machine has shifted out the final bit and executed
 * its `irq nowait 0` instruction.
 *
 * @note Keep it short: it runs inside the ISR.
 * @param ctx  The ctx given to tiku_pio_arch_bitbang_tx().
 */
typedef void (*tiku_pio_done_cb_t)(void *ctx);

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Take PIO0 out of reset, load the bit-bang program at address 0 and
 *        enable PIO0_IRQ_0 in the NVIC.
 *
 * A second call returns at once.
 */
void tiku_pio_arch_init(void);

/**
 * @brief Start a one-shot bit-bang transmission on PIO0 / SM0.
 *
 * Configures the pin as a PIO0 output, sets the SM clock divider so each bit
 * takes @p bit_period_us, loads the data word and bit count, and enables the
 * SM.  Returns immediately; @p on_done fires from the PIO0 IRQ.
 *
 * @param gpio_pin     GPIO number (0..47) to drive
 * @param data         Up to 32 bits in the kernel bit-bang packing:
 *                     MSB-first data starts at bit 31, LSB-first data
 *                     at bit 0
 * @param bit_count    Number of bits to shift out (1..32)
 * @param msb_first    1 = bit 31 of data shifts out first;
 *                     0 = bit 0 first
 * @param bit_period_us  Bit period in microseconds (>= 1); must fit the
 *                       16.8-bit divider at the current system clock
 * @param on_done      Completion callback; may be NULL
 * @param ctx          Opaque pointer passed to on_done
 *
 * @return TIKU_PIO_OK, TIKU_PIO_ERR_BUSY, TIKU_PIO_ERR_INVALID,
 *         or TIKU_PIO_ERR_NOT_READY
 */
int tiku_pio_arch_bitbang_tx(uint8_t  gpio_pin,
                             uint32_t data,
                             uint8_t  bit_count,
                             uint8_t  msb_first,
                             uint16_t bit_period_us,
                             tiku_pio_done_cb_t on_done,
                             void   *ctx);

/**
 * @brief Return non-zero while a transmission is in progress.
 *
 * @return Non-zero from a successful tiku_pio_arch_bitbang_tx() until its
 *         completion interrupt or an abort, 0 otherwise.
 */
int tiku_pio_arch_bitbang_busy(void);

/**
 * @brief Abort the in-progress bit-bang transmission.
 *
 * Disables the SM, drains the TX FIFO, and clears the busy flag.
 * The completion callback is not called.
 *
 * @return TIKU_PIO_OK, or TIKU_PIO_ERR_NOT_READY if no tx is active.
 */
int tiku_pio_arch_bitbang_abort(void);

/**
 * @brief PIO0_IRQ_0 interrupt service routine.
 *
 * Strong override of the weak alias in tiku_crt_early.c, wired automatically
 * when this driver is linked in.  Clears the IRQ source, resets the busy flag
 * and invokes the on_done callback.
 *
 * @note Runs in NVIC ISR context -- keep callbacks short.
 */
void tiku_rp2350_pio0_irq0_handler(void);

#endif /* TIKU_RP2350_PIO_ARCH_H_ */
