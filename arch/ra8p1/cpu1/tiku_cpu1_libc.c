/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_libc.c - the two libc calls the payload's crypto reaches for.
 *
 * The payload links -nostdlib against no C library, and GCC emits calls to
 * these from ordinary struct and array assignments regardless.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;

    while (n-- != 0U) {
        *p++ = (unsigned char)c;
    }
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;

    while (n-- != 0U) {
        *p++ = *q++;
    }
    return d;
}
