/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_thread_arch.c - C5 software-interrupt worker switching and newlib state.
 * C5 interrupt source 23 and INTPRI+0x90 follow ESP-IDF 4d59230.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_thread_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_systimer_arch.h"
#include "tiku_timer_arch.h"
#include <kernel/threads/tiku_thread.h>
#include <reent.h>
#include <string.h>

#define C5_SWITCH_REQUEST 0x600C5090u
extern char __global_pointer$;
extern uint32_t *tiku_thread_switch(uint32_t *frame);
static struct _reent workers[TIKU_THREADS_MAX], interrupt_reent;
static struct _reent *kernel_reent;
static uint8_t reent_ready[TIKU_THREADS_MAX];

/** @brief Clear the request before allowing the scheduler to select a frame. */
static uint32_t *switch_context(uint32_t *frame)
{
    TIKU_C5_REG_WRITE(C5_SWITCH_REQUEST, 0);
    (void)TIKU_C5_REG_READ(C5_SWITCH_REQUEST);
    return tiku_thread_switch(frame);
}

void tiku_thread_arch_boot(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(C5_SWITCH_REQUEST, 0);
    if (tiku_c5_irq_attach_switch(2, 23, switch_context) != 0 ||
        tiku_c5_irq_enable(2, 1) != 0) {
        tiku_c5_fatal("worker IRQ ownership");
    }
    kernel_reent = _impure_ptr;
    _REENT_INIT_PTR(&interrupt_reent);
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_thread_arch_pend(void)
{
    TIKU_C5_REG_WRITE(C5_SWITCH_REQUEST, 1);
    (void)TIKU_C5_REG_READ(C5_SWITCH_REQUEST);
}
uint32_t tiku_thread_arch_cycles(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}
uint32_t *tiku_thread_arch_frame_init(uint32_t *top, void (*entry)(void *),
                                      void *argument, void (*exit_fn)(void))
{
    uintptr_t aligned = (uintptr_t)top & ~(uintptr_t)15u;
    uint32_t *frame = (uint32_t *)(aligned - TIKU_C5_FRAME_SIZE);
    unsigned i;
    for (i = 0; i < TIKU_C5_FRAME_SIZE / 4; i++) {
        frame[i] = 0;
    }
    frame[1] = (uint32_t)(uintptr_t)exit_fn;
    frame[2] = (uint32_t)aligned;
    frame[3] = (uint32_t)(uintptr_t)&__global_pointer$;
    frame[10] = (uint32_t)(uintptr_t)argument;
    frame[TIKU_C5_FRAME_PC / 4] = (uint32_t)(uintptr_t)entry;
    frame[TIKU_C5_FRAME_STATUS / 4] = (3u << 11) | (1u << 7);
    frame[TIKU_C5_FRAME_CAUSE / 4] = (1u << 31) | (3u << 28) | (1u << 27);
    return frame;
}
void tiku_c5_reent_init(uint8_t slot)
{
    if (slot >= TIKU_THREADS_MAX) {
        tiku_c5_fatal("worker reentrancy slot");
    }
    if (reent_ready[slot]) {
        _reclaim_reent(&workers[slot]);
    }
    _REENT_INIT_PTR(&workers[slot]);
    reent_ready[slot] = 1;
}
void tiku_c5_reent_enter(void)
{
    if (kernel_reent != NULL) {
        _impure_ptr = &interrupt_reent;
    }
}
void tiku_c5_reent_leave(void)
{
    tiku_thread_t *thread;
    if (kernel_reent == NULL) {
        return;
    }
    thread = tiku_thread_self();
    _impure_ptr = thread == NULL ? kernel_reent : &workers[thread->slot];
}

/** @brief Return the reentrancy object selected by trap entry or exit. */
struct _reent *__getreent(void)
{
    return _impure_ptr;
}
