/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_arch.c - nRF54L hardware timer (stub — not yet wired)
 *
 * Honest placeholder for the microsecond-class single-shot htimer.
 * init/schedule are no-ops and now() returns 0, so nothing ever fires;
 * the kernel htimer subsystem links but stays inert. A real TIMER/GRTC
 * compare-match backend (with an ISR that calls tiku_htimer_run_next())
 * is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel/timers/tiku_htimer.h>

/**
 * @brief Initialise the hardware timer (stub — no-op).
 *
 * A real backend brings up a TIMER/GRTC compare channel and unmasks
 * its NVIC line here. Until then there is nothing to configure.
 */
void tiku_htimer_arch_init(void)
{
}

/**
 * @brief Read the current hardware-timer tick (stub).
 *
 * No free-running counter is wired yet, so this returns 0 rather than
 * a fabricated tick. Time does not advance; combined with the no-op
 * schedule() below, no htimer ever fires.
 *
 * @return 0 always (no counter available).
 */
tiku_htimer_clock_t tiku_htimer_arch_now(void)
{
    return (tiku_htimer_clock_t)0;
}

/**
 * @brief Arm the compare register for the next interrupt (stub — no-op).
 *
 * A real backend programs a TIMER/GRTC compare to @p t and unmasks the
 * IRQ. Here the request is accepted but never armed, so the callback
 * will not run.
 *
 * @param t  Target 16-bit tick value (ignored).
 */
void tiku_htimer_arch_schedule(tiku_htimer_clock_t t)
{
    (void)t;
}
