/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_hal.h - per-port interrupt masking for the masked tiku_crit window.
 *
 * The defer-only window needs no port support. The masked window calls these
 * two functions to clear and restore peripheral interrupt-enable bits; the
 * port keeps the one snapshot, since one window is held at a time.
 *
 * SPDX-License-Identifier: Apache-2.0
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
 * The port saves the prior state for tiku_crit_arch_unmask_irqs() to restore
 * bit for bit, and leaves global interrupts enabled, so the ISRs of preserved
 * sources keep running.
 *
 * @param preserve_mask  Bitwise OR of TIKU_CRIT_PRESERVE_* flags
 *                       (see kernel/timers/tiku_crit.h).
 */
void tiku_crit_arch_mask_irqs(uint8_t preserve_mask);

/**
 * @brief Restore the IE state captured by the most recent
 *        tiku_crit_arch_mask_irqs() call.
 *
 * @note Call only after a matching tiku_crit_arch_mask_irqs(), one unmask per
 *       mask; without one the result is undefined.
 */
void tiku_crit_arch_unmask_irqs(void);

#endif /* TIKU_CRIT_HAL_H_ */
