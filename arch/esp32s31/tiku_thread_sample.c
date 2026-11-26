/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_sample.c - sample S31 worker registers and stack addresses.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>

extern char __sram_start[], _etext[];
extern char __tiku_s31_irom_start[] __attribute__((weak));
extern char __tiku_s31_irom_end[] __attribute__((weak));

static int code_address(uintptr_t word)
{
    return !(word & 1u) &&
        ((word >= (uintptr_t)__sram_start && word < (uintptr_t)_etext) ||
         (word >= (uintptr_t)__tiku_s31_irom_start && word < (uintptr_t)__tiku_s31_irom_end));
}

size_t tiku_thread_arch_sample(const uint32_t *frame, size_t bytes,
                               uintptr_t *out, size_t capacity, size_t scan_words)
{
    const uint32_t *stack;
    size_t n = 1, available;

    if (capacity == 0 || bytes < S31_FRAME_SIZE) return 0;
    stack = frame + S31_FRAME_SIZE / sizeof *frame;
    if (frame[2] != (uintptr_t)stack) return 0;
    out[0] = frame[S31_FRAME_PC / sizeof *frame];
    if (capacity == 1) return 1;
    out[n++] = frame[1];
    available = (bytes - S31_FRAME_SIZE) / sizeof *stack;
    if (scan_words > available) scan_words = available;
    for (size_t i = 0; i < scan_words && n < capacity; i++) {
        if (code_address(stack[i])) out[n++] = stack[i];
    }
    return n;
}
