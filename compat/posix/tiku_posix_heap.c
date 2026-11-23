/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_heap.c - the bounds of a program's C-library heap over memory
 * the program gives: the break, its growth and its refusals.  The kernel
 * keeps no heap; tiku_posix_sbrk.c hands this one to the C library.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include "tiku_posix.h"
#include <hal/tiku_cpu.h>

static uint8_t *heap_base, *heap_top, *heap_limit;
static uint32_t refusals;           /* growths refused, saturating */
static size_t largest_refused;      /* the largest of them, in bytes */

int tiku_posix_heap_memory(void *base, size_t bytes)
{
    uintptr_t start, end;
    int r = 0;

    if (base == NULL || bytes > UINTPTR_MAX - (uintptr_t)base) return EINVAL;
    start = ((uintptr_t)base + 15u) & ~(uintptr_t)15u;
    end = (uintptr_t)base + bytes;
    if (end < start || end - start < TIKU_POSIX_HEAP_MIN) return EINVAL;
    tiku_atomic_enter();
    if (heap_base != NULL) {
        r = EBUSY;
    } else {
        heap_base = heap_top = (uint8_t *)start;
        heap_limit = (uint8_t *)end;
    }
    tiku_atomic_exit();
    return r;
}

void *tiku_posix_heap_grow(intptr_t increment)
{
    void *old = (void *)-1;

    tiku_atomic_enter();
    if (heap_base != NULL &&
        (increment >= 0 ? (uintptr_t)increment <= (uintptr_t)(heap_limit - heap_top)
                        : (uintptr_t)0 - (uintptr_t)increment <= (uintptr_t)(heap_top - heap_base))) {
        old = heap_top;
        heap_top += increment;
    } else if (increment > 0) {
        if (refusals != UINT32_MAX) refusals++;
        if ((size_t)increment > largest_refused) largest_refused = (size_t)increment;
    }
    tiku_atomic_exit();
    return old;
}

void tiku_posix_heap_use(size_t *used, size_t *limit, uint32_t *refused, size_t *largest)
{
    tiku_atomic_enter();
    if (used != NULL) *used = (size_t)(heap_top - heap_base);
    if (limit != NULL) *limit = (size_t)(heap_limit - heap_base);
    if (refused != NULL) *refused = refusals;
    if (largest != NULL) *largest = largest_refused;
    tiku_atomic_exit();
}
