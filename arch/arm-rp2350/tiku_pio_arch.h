/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pio_arch.h - RP2350 PIO (Programmable I/O) driver
 *
 * The RP2350 has three PIO blocks; each block has four state machines
 * sharing a 32-instruction program memory and 4-deep TX/RX FIFOs.
 * TikuOS uses one state machine on PIO0 as a hardware-offloaded
 * bit-bang engine (the backend for kernel/timers/tiku_bitbang.c).
 *
 * Program (4 instructions, loaded at address 0):
 *   addr 0:  out pins, 1     ; shift 1 bit from OSR to the output pin
 *   addr 1:  jmp x-- 0       ; decrement X, jump back if non-zero
 *   addr 2:  irq nowait 0    ; signal completion to PIO0_IRQ_0
 *   addr 3:  jmp 3           ; halt (waits for CPU to restart SM)
 *
 * CPU side per transmission:
 *   1. Reset SM, load OSR with the data word, load X with bit_count-1.
 *   2. Configure clkdiv for the requested bit period.
 *   3. Enable SM. SM shifts bit_count bits to the pin, fires IRQ.
 *   4. PIO0_IRQ_0 handler invokes the kernel completion callback.
 *
 * Single-shot per call; only one bit-bang transmission can run at a
 * time. Long bursts (> 32 bits) push multiple words; SM auto-pulls
 * on OSR exhaustion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_PIO_ARCH_H_
#define TIKU_RP2350_PIO_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return codes for the PIO bit-bang driver.
 *
 * TIKU_PIO_OK           — operation succeeded.
 * TIKU_PIO_ERR_BUSY     — a transmission is already in progress.
 * TIKU_PIO_ERR_INVALID  — a parameter is out of range (pin > 47,
 *                         bit_count < 1 or > 32, bit_period_us < 1).
 * TIKU_PIO_ERR_NOT_READY — the driver was not initialised, or abort
 *                          was called when no tx was active.
 */
#define TIKU_PIO_OK              0
#define TIKU_PIO_ERR_BUSY       -1
#define TIKU_PIO_ERR_INVALID    -2
#define TIKU_PIO_ERR_NOT_READY  -3

/*---------------------------------------------------------------------------*/
/* CALLBACK                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Completion callback. Runs in PIO0_IRQ_0 ISR context.
 *
 * Invoked once the state machine has shifted out the final bit and
 * executed the `irq 0` instruction.  Keep the callback short: it
 * runs inside the NVIC ISR window.
 */
typedef void (*tiku_pio_done_cb_t)(void *ctx);

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief One-time init: take PIO0 out of reset, install the bitbang
 *        program at address 0.
 *
 * Idempotent. Safe to call multiple times.
 */
void tiku_pio_arch_init(void);

/**
 * @brief Start a one-shot bit-bang transmission on PIO0 / SM0.
 *
 * Configures the pin as a PIO0 output, sets the state machine clock
 * divider so each bit takes @p bit_period_us microseconds, loads
 * the data word and the bit count, and enables the SM. Returns
 * immediately; @p on_done is called from the PIO0 IRQ once the
 * stream completes.
 *
 * @param gpio_pin     GPIO number (0..47) to drive
 * @param data         Up to 32 bits, packed MSB-first if msb_first=1
 *                     else LSB-first (matching the kernel bitbang
 *                     convention)
 * @param bit_count    Number of bits to shift out (1..32)
 * @param msb_first    1 = MSB of data shifts out first;
 *                     0 = LSB-first
 * @param bit_period_us  Bit period in microseconds (>= 1)
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
 * @return Non-zero if the state machine is active, 0 if idle.
 */
int tiku_pio_arch_bitbang_busy(void);

/**
 * @brief Abort the in-progress bitbang transmission.
 *
 * Disables the SM, drains the TX FIFO, and clears the busy flag.
 * The completion callback is NOT invoked.
 *
 * @return TIKU_PIO_OK, or TIKU_PIO_ERR_NOT_READY if no tx is active.
 */
int tiku_pio_arch_bitbang_abort(void);

/**
 * @brief PIO0_IRQ_0 interrupt service routine.
 *
 * Strong override of the weak alias in tiku_crt_early.c.  Wired
 * automatically when this driver is linked in.  Clears the IRQ source,
 * resets the busy flag, and invokes the on_done callback registered
 * with tiku_pio_arch_bitbang_tx().  Runs in NVIC ISR context — keep
 * callbacks short.
 */
void tiku_rp2350_pio0_irq0_handler(void);

#endif /* TIKU_RP2350_PIO_ARCH_H_ */
