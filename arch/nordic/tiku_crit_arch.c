/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crit_arch.c - nRF54L IRQ-mask backend for tiku_crit
 *
 * Critical sections use PRIMASK (mask all maskable interrupts) rather than the
 * rp2350 port's selective NVIC ISER/ICER snapshot.  Rationale: nRF54L external
 * IRQs span nine NVIC ISER words (IRQs 0..271) and, at this stage of the port,
 * the only live NVIC source is the TIMER10 tick -- the console UART is polled
 * and the htimer/GPIO IRQs are stubbed.  PRIMASK is the textbook-atomic choice
 * and cannot leave an IRQ half-masked; the preserve_mask (which keeps selected
 * sources live across the window) is a later optimisation and is ignored here.
 *
 * Not nesting-safe (single saved slot), matching the rp2350 backend -- the
 * kernel brackets short, non-nested critical sections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_crit_hal.h>
#include <kernel/timers/tiku_crit.h>
#include <arch/nordic/tiku_nordic_core.h>
#include <stdint.h>

/** @brief PRIMASK snapshot taken at mask time, restored on unmask. */
static uint32_t tiku_nordic_crit_primask;

void tiku_crit_arch_mask_irqs(uint8_t preserve_mask)
{
    (void)preserve_mask;   /* selective preserve not implemented yet */

    tiku_nordic_crit_primask = tiku_nordic_get_primask();
    tiku_nordic_disable_irq();
}

void tiku_crit_arch_unmask_irqs(void)
{
    tiku_nordic_set_primask(tiku_nordic_crit_primask);
}
