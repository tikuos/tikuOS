/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_compute_p4_state.h - PIE state access for compute calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_COMPUTE_P4_STATE_H_
#define TIKU_COMPUTE_P4_STATE_H_
#include <stdint.h>
/** @brief Read the calling context's PIE state. */
static inline uint32_t tiku_compute_p4_state_get(void)
{
    uint32_t state;
    __asm__ volatile ("csrr %0, 0x7f2" : "=r"(state) : : "memory");
    return state;
}
/** @brief Restore an entry state that was not dirty. */
static inline void tiku_compute_p4_state_restore(uint32_t state)
{
    if ((state & 3U) != 3U) {
        __asm__ volatile ("csrw 0x7f2, %0" : : "r"(state) : "memory");
    }
}
#endif
