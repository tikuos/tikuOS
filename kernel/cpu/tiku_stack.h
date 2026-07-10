/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_stack.h - stack high-water measurement by painting.
 *
 * /sys/mem/free reports the LIVE gap under the current SP -- headroom at this
 * instant.  It cannot tell you how close the stack has EVER come to overflow,
 * which is the number that matters for sizing.  Painting does: fill the
 * unused stack with a sentinel early in boot; the deepest the stack ever
 * reached is the lowest overwritten sentinel, and the intact cushion below it
 * is the worst-case headroom -- measured, not guessed.  Pairs with the MPU
 * stack guard: the guard catches an overflow, this warns before one.
 *
 * SAFETY: painting is bounded by the ARCH-DECLARED stack bottom
 * (tiku_stack_arch_bottom), not by _end.  The gap between _end and the stack
 * holds the heap and the armed no-access MPU guard region -- painting or
 * scanning through those faults the CPU (the lesson of the first attempt).
 * The arch override lives next to the guard-arming code, computed from the
 * SAME expression, so the paint floor and the guard top cannot diverge.  On
 * an arch with no override the weak default returns 0 and the whole feature
 * is dormant: nothing painted, stack_free reports 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STACK_H_
#define TIKU_STACK_H_

#include <stdint.h>

/**
 * @brief Lowest paintable stack address (just above the MPU stack guard).
 *
 * Weak; the arch MPU backend overrides it beside its guard-arming code.
 * A return of 0 means "bounds unknown": painting and measurement no-op.
 */
uint32_t tiku_stack_arch_bottom(void);

/**
 * @brief Paint the unused stack with the sentinel pattern.
 *
 * Call ONCE, early in boot (shallow call depth = the most stack painted).
 * Fills [tiku_stack_arch_bottom(), SP - margin); no-op when the arch bottom
 * is unknown (0).
 */
void tiku_stack_paint(void);

/**
 * @brief Worst-case stack headroom seen since tiku_stack_paint(), in bytes.
 *
 * The intact sentinel cushion above the arch stack bottom -- the closest the
 * stack has ever come to the guard.  Monotonically non-increasing.  0 when
 * the arch bottom is unknown (feature dormant) or, alarmingly, when the
 * stack has already consumed its whole budget.
 */
uint32_t tiku_stack_free(void);

#endif /* TIKU_STACK_H_ */
