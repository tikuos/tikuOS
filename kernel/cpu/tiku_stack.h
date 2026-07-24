/*
 * Tiku Operating System v0.06
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
#include <stddef.h>

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

#if defined(TIKU_STACK_TEST_HOOKS) && TIKU_STACK_TEST_HOOKS
/**
 * @brief TEST-ONLY hook: paint an explicit range with the sentinel.
 *
 * The same painter tiku_stack_paint() uses, but on caller-supplied
 * bounds, so the test suite can exercise it over a plain buffer
 * instead of the live stack (whose bounds are arch-supplied and whose
 * result is unrepeatable).  Not part of the normal API; compiled only
 * when TIKU_STACK_TEST_HOOKS is set.  Fills the word-aligned range
 * [bottom, sp - margin); a no-op when @p bottom is 0 or the range is
 * no larger than @p margin.
 *
 * @param bottom  Lowest address to paint (0 = no-op)
 * @param sp      Simulated stack pointer; painting stops @p margin below
 * @param margin  Bytes left unpainted just below @p sp
 */
void tiku_stack_test_paint(uintptr_t bottom, uintptr_t sp, uint32_t margin);

/**
 * @brief TEST-ONLY hook: measure the intact cushion in an explicit range.
 *
 * The same scanner tiku_stack_free() uses, on caller-supplied bounds;
 * test suite only, compiled with TIKU_STACK_TEST_HOOKS.  Counts intact
 * sentinel words upward from @p bottom and stops at the first
 * overwritten word — the deepest point the "stack" ever reached — or
 * at @p sp, which is never read at or above.
 *
 * @param bottom  Lowest address of the painted range (0 = returns 0)
 * @param sp      Upper bound of the scan
 * @return Bytes of intact sentinel above @p bottom; 0 if @p bottom is 0
 *         or @p sp is not above it
 */
uint32_t tiku_stack_test_free(uintptr_t bottom, uintptr_t sp);
#endif

#endif /* TIKU_STACK_H_ */
