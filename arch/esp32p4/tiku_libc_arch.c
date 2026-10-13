/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_libc_arch.c - Refuse libc heap growth in the storage-free RAM profile.
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
