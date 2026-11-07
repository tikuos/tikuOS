/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_p4_scope.h - preserve configuration around vector library calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_P4_SCOPE_H_
#define TIKU_COMPUTE_P4_SCOPE_H_
#include <arch/esp32p4/tiku_pie_arch.h>
#include "tiku_compute_p4_state.h"

/** @brief Caller-owned configuration and hardware-loop register snapshot. */
typedef struct {
    uint32_t pie, cfg, state, loop[6];
} tiku_compute_p4_scope_t;

/** @brief Snapshot enabled units; return zero if PIE or hardware loops are off. */
static inline int tiku_compute_p4_scope_enter(tiku_compute_p4_scope_t *s)
{
    s->pie = tiku_compute_p4_state_get();
    __asm__ volatile ("csrr %0, 0x7f1" : "=r"(s->state));
    if ((s->pie & 3U) == 0 || (s->state & 3U) == 0) {
        return 0;
    }
    s->cfg = tiku_p4_pie_cfg_get();
    __asm__ volatile ("csrr %0, 0x7c6" : "=r"(s->loop[0]));
    __asm__ volatile ("csrr %0, 0x7c7" : "=r"(s->loop[1]));
    __asm__ volatile ("csrr %0, 0x7c8" : "=r"(s->loop[2]));
    __asm__ volatile ("csrr %0, 0x7c9" : "=r"(s->loop[3]));
    __asm__ volatile ("csrr %0, 0x7ca" : "=r"(s->loop[4]));
    __asm__ volatile ("csrr %0, 0x7cb" : "=r"(s->loop[5]));
    /* Library loops execute with empty slots; caller counts remain in the snapshot. */
    __asm__ volatile ("csrwi 0x7f1, 1\ncsrw 0x7c8, zero\ncsrw 0x7cb, zero\ncsrwi 0x7f1, 1" ::: "memory");
    return 1;
}

/** @brief Restore configuration and loops; vector data registers are call-clobbered. */
static inline void tiku_compute_p4_scope_leave(const tiku_compute_p4_scope_t *s)
{
    uint32_t state = s->state;
    __asm__ volatile ("csrw 0x7c6, %0" :: "r"(s->loop[0]) : "memory");
    __asm__ volatile ("csrw 0x7c7, %0" :: "r"(s->loop[1]) : "memory");
    __asm__ volatile ("csrw 0x7c8, %0" :: "r"(s->loop[2]) : "memory");
    __asm__ volatile ("csrw 0x7c9, %0" :: "r"(s->loop[3]) : "memory");
    __asm__ volatile ("csrw 0x7ca, %0" :: "r"(s->loop[4]) : "memory");
    __asm__ volatile ("csrw 0x7cb, %0" :: "r"(s->loop[5]) : "memory");
    if (s->loop[2] != 0 || s->loop[5] != 0) {
        state |= 3U;
    }
    __asm__ volatile ("csrw 0x7f1, %0" :: "r"(state) : "memory");
    tiku_p4_pie_cfg_set(s->cfg);
    tiku_compute_p4_state_restore(s->pie);
}
#endif
