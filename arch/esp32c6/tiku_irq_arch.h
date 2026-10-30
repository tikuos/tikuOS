/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_arch.h - C6 owned peripheral and opt-in radio interrupt lines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_IRQ_ARCH_H
#define TIKU_C6_IRQ_ARCH_H
#include "tiku_io_arch.h"
#ifndef TIKU_C6_IRQ_LAST
#define TIKU_C6_IRQ_LAST 23u
#endif
#if TIKU_C6_IRQ_LAST < 23 || TIKU_C6_IRQ_LAST > 31
#error "C6 owned interrupt range must end between lines23 and31"
#endif
typedef void (*tiku_c6_isr_t)(void);
/** @brief Claim a disabled level line19..TIKU_C6_IRQ_LAST and a source0..76.
 * @return Zero on success, negative for invalid, routed or occupied input.
 */
int tiku_c6_irq_attach(unsigned line, unsigned source, unsigned priority, tiku_c6_isr_t handler);
/** @brief Enable or mask an owned line; return zero, or -1 if unowned. */
int tiku_c6_irq_enable(unsigned line, int enabled);
/** @brief Return nonzero for a matching owned handler and source route. */
int tiku_c6_irq_owned(unsigned line, unsigned source, tiku_c6_isr_t handler);
/** @brief Mask and restore the registers saved when the line was claimed. */
void tiku_c6_irq_detach(unsigned line);
/** @brief Dispatch an owned machine interrupt; return one when handled. */
int tiku_c6_irq_trap(uint32_t cause);
#endif
