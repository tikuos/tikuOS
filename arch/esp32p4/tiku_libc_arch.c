/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_libc_arch.c - Refuse libc heap growth on the P4.
 *
 * _sbrk() fails every request with errno ENOMEM, so newlib's malloc() takes
 * no SRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <errno.h>

/** @brief Report ENOMEM without assigning SRAM to a libc heap. */
void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}
