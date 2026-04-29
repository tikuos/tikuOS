/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_hal.h - Hardware abstraction layer interface for the
 *                   critical-execution window's IRQ-mask flavour
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_crit_hal.h
 * @brief Platform-agnostic IRQ-mask interface for tiku_crit
 *
 * The defer-only flavour of the critical-execution window
 * (tiku_crit_begin_defer) needs no platform support — it just flips
 * a flag the timer dispatcher and clock ISR consult. The masked
 * flavour (tiku_crit_begin) has to clear and later restore real
 * peripheral IE bits. That part is platform-specific and lives
 * behind these two hooks.
 *
 * The platform implementation (e.g. arch/msp430/tiku_crit_arch.c)
 * owns the snapshot storage. Only one window is held at a time, so
 * a single static save area inside the arch file is sufficient.
 *
 * Contract:
 *   - mask_irqs() must snapshot the current state of every
 *     peripheral IE family the platform exposes, then clear each
 *     family whose TIKU_CRIT_PRESERVE_* flag is NOT set in
 *     preserve_mask.
 *   - unmask_irqs() must restore exactly what mask_irqs() snapshot.
 *   - GIE (or the platform equivalent) must remain enabled across
 *     both calls so any preserved ISR keeps firing.
 *   - The TIKU_CRIT_PRESERVE_* flag values are defined in
 *     kernel/timers/tiku_crit.h and are stable across platforms;
 *     the arch implementation maps each flag to whichever IE
 *     register family covers that role on the device.
 */

#ifndef TIKU_CRIT_HAL_H_
#define TIKU_CRIT_HAL_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* REQUIRED PLATFORM FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Snapshot and clear every peripheral IE family not in
 *        @p preserve_mask.
 *
 * Called from tiku_crit_begin() once the kernel has decided a
 * masked window is wanted. The platform must store the prior IE
 * state internally so that tiku_crit_arch_unmask_irqs() can
 * restore it bit-for-bit.
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
