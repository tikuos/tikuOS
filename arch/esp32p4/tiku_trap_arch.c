/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trap_arch.c - P4 interrupt dispatch from the trap entry.
 *
 * tiku_p4_trap_dispatch() takes the frame tiku_crt_early.S saved for an
 * interrupt and returns the frame to resume, which a worker switch may
 * replace.  Exceptions go to tiku_p4_fault_capture() instead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_irq_arch.h"
#if (TIKU_THREADS_ENABLE + 0)
#include "tiku_thread_arch.h"
#endif

/** @brief Dispatch a CLIC interrupt frame already validated by trap entry.
 */
uint32_t *tiku_p4_trap_dispatch(uint32_t *frame)
{
    uint32_t cause = frame[TIKU_P4_FRAME_CAUSE / 4];

    if (cause & 0x80000000u) {
#if (TIKU_THREADS_ENABLE + 0)
        tiku_p4_reent_enter();
#endif
        frame = tiku_p4_irq_dispatch_frame(cause & 0xFFFu, frame);
#if (TIKU_THREADS_ENABLE + 0)
        tiku_p4_reent_leave();
#endif
    }
    return frame;
}
