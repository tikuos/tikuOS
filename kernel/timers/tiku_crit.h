/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit.h - Critical execution window
 *
 * A kernel-aware, bounded-duration critical section. Two flavours
 * are exposed because the right answer depends on the caller's
 * timing budget AND on whether other peripherals must keep working
 * during the window:
 *
 *   tiku_crit_begin_defer()   -- defer software-timer dispatch
 *                                only. Touches NO peripheral IE
 *                                bits. UART, ADC, GPIO IRQs etc.
 *                                all keep firing. Cooperative-
 *                                friendly and minimum-impact;
 *                                accept ISR jitter as the cost.
 *
 *   tiku_crit_begin()         -- defer + clear every known
 *                                peripheral IE bit not listed in
 *                                preserve_mask. Zero peripheral
 *                                jitter; the cost is that masked
 *                                subsystems are paused for the
 *                                window's duration (UART RX
 *                                drops bytes, GPIO edges may
 *                                collapse to one, etc.).
 *
 * Both share tiku_crit_end() and the tiku_crit_active() flag.
 *
 * Why two? TikuOS uses cooperative protothreads, so "the scheduler
 * cannot run other processes while a window is held" is not a
 * concern -- the caller is already parked in PT_WAIT_UNTIL by the
 * time the window opens. The remaining cost of masking is purely
 * the loss of fidelity for whatever each masked ISR was tracking.
 * For some workloads (high-rate bit-bang) that loss is worth the
 * jitter savings; for others (slow bit-bang with active UART) it
 * is not. Letting the caller pick keeps both honest.
 *
 * Use cases (non-exhaustive):
 *   - tiku_crit_begin_defer:  ~kHz software UART, slow IR remote,
 *                             dispatcher-bound timing where
 *                             individual ISRs are not the bottleneck.
 *   - tiku_crit_begin:        backscatter symbol streams, software
 *                             SPI at MHz, sub-microsecond bit
 *                             timing where any extra ISR would
 *                             break the protocol.
 *
 * Common semantics during a held window (either flavour):
 *   - The software-timer dispatcher early-exits if polled.
 *   - The Timer A0 ISR suppresses tiku_timer_request_poll().
 *   - The process scheduler is NOT frozen; tiku_process_post()
 *     calls during the window queue up exactly as today.
 *   - Global interrupts (GIE) stay ENABLED. We never blanket-mask
 *     because the bit-clock ISR (htimer) must keep firing.
 *
 * Watchdog note: the watchdog RESET (not the interval interrupt)
 * is hardware and not maskable. Hold windows must be shorter than
 * the configured WDT timeout regardless of which flavour is used.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CRIT_H_
#define TIKU_CRIT_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_CRIT_OK             0
#define TIKU_CRIT_ERR_BUSY      -1  /**< A window is already held */
#define TIKU_CRIT_ERR_NOT_HELD  -2  /**< End called without a matching begin */

/*---------------------------------------------------------------------------*/
/* PRESERVE FLAGS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * preserve_mask is interpreted ONLY by tiku_crit_begin() (the masked
 * flavour). tiku_crit_begin_defer() ignores any flags because it
 * does not touch IE bits at all.
 *
 * Flags name *families* of interrupt sources. The implementation
 * may mask several IE registers per family (e.g. UART covers both
 * UCA0 and UCA1 if present on the device).
 */

/** Bit-clock ISR (Timer A1 CCR0). Required for any bit-bang
 *  transmitter built on tiku_htimer. */
#define TIKU_CRIT_PRESERVE_HTIMER  (1u << 0)

/** System tick ISR (Timer A0 CCR0). Preserve to keep
 *  tiku_clock_time() advancing accurately during the window. */
#define TIKU_CRIT_PRESERVE_TICK    (1u << 1)

/** UART RX/TX ISRs (eUSCI_A modules). Preserve to keep shell
 *  input, SLIP frames, NMEA streams, etc. flowing. */
#define TIKU_CRIT_PRESERVE_UART    (1u << 2)

/** I2C / SPI ISRs (eUSCI_B modules). Preserve if a bus
 *  transaction must complete during the window. */
#define TIKU_CRIT_PRESERVE_I2C     (1u << 3)

/** ADC done ISR. Preserve if a conversion is in flight and the
 *  caller will read the result via the ISR. */
#define TIKU_CRIT_PRESERVE_ADC     (1u << 4)

/** Watchdog interval-mode ISR. Preserve if the application is
 *  using the WDT as a periodic timer rather than a reset source. */
#define TIKU_CRIT_PRESERVE_WDT     (1u << 5)

/** External pin edge ISRs (P1IE..P4IE). Preserve to keep button
 *  presses and sensor pulses from being collapsed to a single
 *  edge across the window. */
#define TIKU_CRIT_PRESERVE_GPIO    (1u << 6)

/** Convenience: keep bit-clock alive (typical for any bit-bang
 *  caller). Equivalent to TIKU_CRIT_PRESERVE_HTIMER. */
#define TIKU_CRIT_PRESERVE_BITBANG TIKU_CRIT_PRESERVE_HTIMER

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE (read-only outside this module)                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Held flag, exposed for fast-path inline reads.
 *
 * Defined in tiku_crit.c. Read by ISRs and the timer dispatcher via
 * tiku_crit_active(); never write directly -- use begin/end.
 */
extern volatile uint8_t tiku_crit_held;

/*---------------------------------------------------------------------------*/
/* CORE API                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Open a masked critical-execution window (strict mode).
 * @param max_us         Upper bound on duration, in microseconds. 0
 *                       disables the bound (violation counter is
 *                       not incremented).
 * @param preserve_mask  Bitwise OR of TIKU_CRIT_PRESERVE_* flags.
 *                       Every other peripheral IRQ family the
 *                       module knows about has its enable bit
 *                       cleared. Pass at minimum
 *                       TIKU_CRIT_PRESERVE_HTIMER for bit-bang.
 * @return TIKU_CRIT_OK, or TIKU_CRIT_ERR_BUSY if a window is held.
 *
 * Use when you need precise edge timing and accept that masked
 * subsystems pause for the window. Windows do not nest.
 */
int tiku_crit_begin(uint16_t max_us, uint8_t preserve_mask);

/**
 * @brief Open a defer-only critical-execution window (cooperative).
 * @param max_us  Upper bound on duration, in microseconds. 0
 *                disables the bound.
 * @return TIKU_CRIT_OK, or TIKU_CRIT_ERR_BUSY if a window is held.
 *
 * Sets the held flag so the software-timer dispatcher and the
 * tick ISR's poll-up suppress themselves, but does NOT clear any
 * peripheral IE bit. Hardware ISRs continue to fire and may
 * inject jitter into bit-bang edges (typically 3-5 us per
 * Timer A0 tick). Windows do not nest.
 */
int tiku_crit_begin_defer(uint16_t max_us);

/**
 * @brief Close the held critical-execution window.
 * @return TIKU_CRIT_OK, or TIKU_CRIT_ERR_NOT_HELD if none is open.
 *
 * Restores any IE bits that were masked at begin (no-op for the
 * defer flavour) and drains the timer-process by re-issuing a
 * poll. If the elapsed htimer count exceeded max_us the
 * violation counter advances.
 */
int tiku_crit_end(void);

/**
 * @brief Return non-zero if a window is currently held.
 *
 * Inlined for the hot path (clock ISR, timer dispatcher).
 */
static inline int tiku_crit_active(void)
{
    return tiku_crit_held != 0;
}

/**
 * @brief Total number of windows that exceeded their declared bound.
 *
 * Sticky counter, useful for offline debugging via /proc or shell.
 */
uint16_t tiku_crit_violation_count(void);

/**
 * @brief Total number of windows entered since boot.
 */
uint16_t tiku_crit_enter_count(void);

#endif /* TIKU_CRIT_H_ */
