/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_noheap.c - refuse the C library a heap.
 *
 * TikuOS allocates statically or from a tier.  The C library still reaches for
 * malloc from its printf path, and the stub that satisfies it grows from `end`
 * without a bound -- which is where the SRAM tier is carved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

/**
 * @brief Deny the C library's heap-growth request.
 *
 * Weak because MSP430 rebuilds and links libnosys.a explicitly, so a strong
 * definition collides there; the library stub stays on that part, where the
 * tier is a static array and `end` falls past it.
 *
 * @note An object-file definition, weak included, satisfies the reference, so
 *       the archive member is not pulled and this is what malloc gets.
 * @param incr  Bytes requested (ignored)
 * @return (void *)-1 always, which malloc reports to its caller as NULL
 */
__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    return (void *)-1;
}
