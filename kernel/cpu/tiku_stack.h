/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_stack.h - stack high-water measurement by painting.
 *
 * Boot fills the unused stack with a sentinel; tiku_stack_free() counts the
 * words still intact above tiku_stack_arch_bottom(), the least headroom the
 * stack has had since.  /sys/mem/stack_free reports it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STACK_H_
#define TIKU_STACK_H_

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Lowest stack address the painter fills, as the port defines it.
 *
 * On most ports it is the top of the MPU stack guard; the heap and the guard
 * lie below it.  The weak default returns 0, and with 0 tiku_stack_paint()
 * does nothing and tiku_stack_free() returns 0.
 */
uint32_t tiku_stack_arch_bottom(void);

/**
 * @brief Paint the unused stack with the sentinel pattern.
 *
 * Fills [tiku_stack_arch_bottom(), SP - margin); no-op when the arch bottom
 * is unknown (0).
 *
 * @note Call once, early in boot: the shallower the call depth, the more
 *       stack is painted.
 */
void tiku_stack_paint(void);

/**
 * @brief Worst-case stack headroom seen since tiku_stack_paint(), in bytes.
 *
 * The intact sentinel cushion above the arch stack bottom -- the closest the
 * stack has ever come to the guard, and monotonically non-increasing.  0 when
 * the feature is dormant or the whole stack has been used.
 */
uint32_t tiku_stack_free(void);

#if defined(TIKU_STACK_TEST_HOOKS) && TIKU_STACK_TEST_HOOKS
/**
 * @brief Test-only hook: paint an explicit range with the sentinel.
 *
 * Runs the painter on caller-supplied bounds, such as a test buffer.  Fills
 * [bottom, sp - margin), and does nothing when @p bottom is 0 or the range is
 * no larger than @p margin.
 *
 * @param bottom  Lowest address to paint (0 = no-op)
 * @param sp      Simulated stack pointer; painting stops @p margin below
 * @param margin  Bytes left unpainted just below @p sp
 */
void tiku_stack_test_paint(uintptr_t bottom, uintptr_t sp, uint32_t margin);

/**
 * @brief Test-only hook: measure the intact cushion in an explicit range.
 *
 * The same scanner on caller-supplied bounds: counts intact sentinel words
 * upward from @p bottom and stops at the first overwritten one -- the deepest
 * point reached -- or at @p sp, which is never read at or above.
 *
 * @param bottom  Lowest address of the painted range (0 = returns 0)
 * @param sp      Upper bound of the scan
 * @return Bytes of intact sentinel above @p bottom; 0 if @p bottom is 0
 *         or @p sp is not above it
 */
uint32_t tiku_stack_test_free(uintptr_t bottom, uintptr_t sp);
#endif

#endif /* TIKU_STACK_H_ */
