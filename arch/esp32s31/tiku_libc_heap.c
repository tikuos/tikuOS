/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_libc_heap.c - libc heap refusal and fatal abort for the local profile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stddef.h>
#include "tiku_runtime_arch.h"

/** @brief Refuse libc heap growth; firmware allocations use memory tiers. */
void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}

/** @brief Halt an aborted libc operation through the resident firmware handler. */
void abort(void)
{
    tiku_s31_fatal("firmware abort");
}

/** @brief Report the assertion line and halt through the firmware UART handler. */
void __assert_func(const char *file, int line, const char *function,
                   const char *expression)
{
    (void)file;
    (void)function;
    tiku_s31_report("assert_line", (uint32_t)line);
    tiku_s31_fatal(expression ? expression : "libc assertion");
}
