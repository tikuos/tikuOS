/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_trap_arch.c - C5 interrupt dispatch and terminal exception reporting.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_irq_arch.h"
#if (TIKU_THREADS_ENABLE + 0)
#include "tiku_thread_arch.h"
#endif

extern void tiku_esp32c5_diagnostic_fault(uint32_t cause, uint32_t pc,
                                         uint32_t value)
    __attribute__((noreturn));

/** @brief Dispatch a CLIC interrupt; exceptions terminate in the fault handler. */
uint32_t *tiku_c5_trap_dispatch(uint32_t *frame)
{
    uint32_t cause = frame[TIKU_C5_FRAME_CAUSE / 4];

    if (cause & 0x80000000u) {
#if (TIKU_THREADS_ENABLE + 0)
        tiku_c5_reent_enter();
#endif
        frame = tiku_c5_irq_dispatch_frame(cause & 0xFFFu, frame);
#if (TIKU_THREADS_ENABLE + 0)
        tiku_c5_reent_leave();
#endif
    } else {
        uint32_t value;
        __asm__ volatile ("csrr %0, mtval" : "=r"(value));
        tiku_esp32c5_diagnostic_fault(cause, frame[TIKU_C5_FRAME_PC / 4], value);
    }
    return frame;
}
