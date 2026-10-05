/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.h - MSP430 hardware timer architecture interface
 *
 * Includes the htimer configuration (for TIKU_HTIMER_ARCH_SECOND) and
 * declares the MSP430-only htimer functions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_HTIMER_ARCH_H_
#define TIKU_HTIMER_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_htimer_config.h"
#include <kernel/timers/tiku_htimer.h>

/* tiku_htimer_config.h must define TIKU_HTIMER_ARCH_SECOND. */
#ifndef TIKU_HTIMER_ARCH_SECOND
#error "TIKU_HTIMER_ARCH_SECOND not defined by tiku_htimer_config.h"
#endif

/*---------------------------------------------------------------------------*/
/* MSP430-SPECIFIC FUNCTIONS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure ACLK source if using ACLK for timer
 *
 * Changes ACLK for every ACLK user, the system tick included.
 *
 * @note Call before tiku_htimer_arch_init() when the timer runs from ACLK.
 */
void tiku_htimer_arch_configure_aclk(void);

/**
 * @brief Check if timer interrupt is pending
 * @return Non-zero if interrupt flag is set
 */
int tiku_htimer_arch_interrupt_pending(void);

/**
 * @brief Get timer configuration register value
 * @return Timer control register (for debugging)
 */
unsigned int tiku_htimer_arch_get_timer_config(void);

/**
 * @brief Print current timer configuration
 *
 * Prints clock source, dividers and calculated frequency through
 * HTIMER_ARCH_PRINTF, which is empty unless DEBUG_HTIMER is set.
 */
void tiku_htimer_arch_print_config(void);

/**
 * @brief Reset timer counter to zero
 *
 * Returns with interrupts enabled, whatever their state before.
 *
 * @warning Pending htimer deadlines are absolute counter values, so after
 *          the reset they fire late.
 */
void tiku_htimer_arch_reset_counter(void);

#endif /* TIKU_HTIMER_ARCH_H_ */
