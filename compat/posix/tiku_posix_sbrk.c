/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_sbrk.c - newlib's heap hook and allocator lock over the
 * library's heap: malloc grows only through _sbrk_r(), so the kernel's
 * refusing _sbrk() is not reached.  An allocation is a short masked
 * section (malloc and free update their lists in it, realloc copies
 * outside it), so any thread, the kernel thread too, may allocate.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include "tiku_posix.h"
#include <hal/tiku_cpu.h>

void *_sbrk_r(struct _reent *r, ptrdiff_t increment)
{
    void *old = tiku_posix_heap_grow((intptr_t)increment);

    if (old == (void *)-1 && r != NULL) _REENT_ERRNO(r) = ENOMEM;
    return old;
}

void __malloc_lock(struct _reent *r)
{
    (void)r;
    tiku_atomic_enter();
}

void __malloc_unlock(struct _reent *r)
{
    (void)r;
    tiku_atomic_exit();
}
