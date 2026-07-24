/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_stack.c - stack high-water measurement by painting.  See tiku_stack.h
 * for the safety contract: everything here stays inside
 * [tiku_stack_arch_bottom(), SP), the arch-declared true stack region --
 * never the heap, never the armed MPU guard below it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_stack.h"
#include "tiku.h"
#include <hal/tiku_compiler.h>   /* TIKU_WEAK */

/* Word sentinel the unused stack is filled with. */
#define TIKU_STACK_PAINT    0xC5C5C5C5u

/* Keep this much below the live SP unpainted: the painter's own frame plus
 * slop for an interrupt arriving mid-paint. */
#define TIKU_STACK_MARGIN   128u

/* Current stack pointer, platform-branched like /sys/mem/free's reader. */
static uintptr_t stack_sp(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_NORDIC)
    uintptr_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    return sp;
#elif defined(PLATFORM_MSP430)
    uint16_t sp;
    __asm__ volatile ("mov r1, %0" : "=r"(sp));
    return (uintptr_t)sp;
#else
    return 0u;
#endif
}

/* Weak default: bounds unknown -> the feature is dormant (nothing painted,
 * tiku_stack_free() == 0).  Arch MPU backends override this right beside
 * their guard-arming code. */
TIKU_WEAK uint32_t tiku_stack_arch_bottom(void)
{
    return 0u;
}

static void stack_paint_between(uintptr_t bottom, uintptr_t sp, uint32_t margin)
{
    uint32_t *lo, *hi;

    if (bottom == 0u || sp <= bottom + margin) {
        return;   /* unwired arch, or no room to paint */
    }

    lo = (uint32_t *)((bottom + 3u) & ~(uintptr_t)3u);
    hi = (uint32_t *)((sp - margin) & ~(uintptr_t)3u);
    while (lo < hi) {
        *lo++ = TIKU_STACK_PAINT;
    }
}

static uint32_t stack_free_between(uintptr_t bottom, uintptr_t sp)
{
    const uint32_t *p, *hi;
    uint32_t  n = 0u;

    if (bottom == 0u || sp <= bottom) {
        return 0u;
    }

    /* Count intact sentinels upward from the stack bottom; the first
     * overwritten word is the deepest the stack has ever reached.  The scan
     * is capped at the live SP, so it never reads outside the stack. */
    p  = (const uint32_t *)((bottom + 3u) & ~(uintptr_t)3u);
    hi = (const uint32_t *)(sp & ~(uintptr_t)3u);
    while (p < hi && *p == TIKU_STACK_PAINT) {
        p++;
        n += 4u;
    }
    return n;
}

void tiku_stack_paint(void)
{
    stack_paint_between((uintptr_t)tiku_stack_arch_bottom(), stack_sp(),
                        TIKU_STACK_MARGIN);
}

uint32_t tiku_stack_free(void)
{
    return stack_free_between((uintptr_t)tiku_stack_arch_bottom(), stack_sp());
}

#if defined(TIKU_STACK_TEST_HOOKS) && TIKU_STACK_TEST_HOOKS
void tiku_stack_test_paint(uintptr_t bottom, uintptr_t sp, uint32_t margin)
{
    stack_paint_between(bottom, sp, margin);
}

uint32_t tiku_stack_test_free(uintptr_t bottom, uintptr_t sp)
{
    return stack_free_between(bottom, sp);
}
#endif
