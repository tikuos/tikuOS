/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_hal.h - Hardware abstraction layer interface for the
 *                   critical-execution window's IRQ-mask flavour
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_crit_hal.h
 * @brief Platform-agnostic IRQ-mask interface for tiku_crit.
 *
 * The defer-only window needs no platform support; the masked one has to clear
 * and restore real peripheral IE bits, which is what these two hooks do.  The
 * arch side owns the snapshot storage, since only one window is held at a time.
 */

#ifndef TIKU_CRIT_HAL_H_
#define TIKU_CRIT_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Snapshot and clear every peripheral IE family not in @p preserve_mask.
 *
 * The platform stores the prior state internally so the unmask call can restore
 * it bit for bit, and must leave global interrupts enabled so a preserved ISR
 * keeps firing.
 *
 * @param preserve_mask  Bitwise OR of TIKU_CRIT_PRESERVE_* flags
 *                       (see kernel/timers/tiku_crit.h).
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask);

/**
 * @brief Restore the IE state captured by the most recent
 *        tiku_crit_arch_mask_irqs() call.
 *
 * Pairs 1:1 with tiku_crit_arch_mask_irqs(). Calling this without
 * a matching mask is undefined.
 */
void tiku_crit_arch_unmask_irqs(void);

#endif /* TIKU_CRIT_HAL_H_ */
