/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_arch.h - C6 owned peripheral interrupt lines 19..23.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_IRQ_ARCH_H
#define TIKU_C6_IRQ_ARCH_H
#include "tiku_io_arch.h"
typedef void (*tiku_c6_isr_t)(void);
/** @brief Claim a disabled level line19..23 and an unrouted source0..76.
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
