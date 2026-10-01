/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - ESP32-C61 worker-thread switcher.
 *
 * PendSV's part goes to a software-raised interrupt at the lowest CLIC
 * level: handlers never nest, so it is taken only once the others return.
 * The trap entry saves whole frames, so a switch is choosing another one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_crt_early.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"

/** Policy hop: kernel/threads/tiku_thread.c picks the next context. */
extern uint32_t *tiku_thread_switch(uint32_t *old_sp);

/* What mret needs to land a fresh worker at its entry: machine mode,
 * interrupts on once it runs (MPIE), at no interrupt level (MPIL 0) --
 * and the interrupt bit, without which this core leaves the level where
 * the switch put it, and no later switch can preempt the worker. */
#define MSTATUS_MPP_M    (3UL << 11)
#define MSTATUS_MPIE     (1UL << 7)
#define MCAUSE_INTERRUPT (1UL << 31)
#define MCAUSE_MPP_M     (3UL << 28)
#define MCAUSE_MPIE      (1UL << 27)

/** @brief The switch: drop the request, then hand the frame to the policy. */
static uint32_t *thread_switch_isr(uint32_t *frame) {
    TIKU_REG32(ESP32C61_INTPRI_FROM_CPU0) = 0UL;
    (void)TIKU_REG32(ESP32C61_INTPRI_FROM_CPU0);
    return tiku_thread_switch(frame);
}

/** @brief One-time bring-up: the switch line; nothing to migrate. */
void tiku_thread_arch_boot(void) {
    TIKU_REG32(ESP32C61_INTPRI_FROM_CPU0) = 0UL;
    tiku_esp32c61_irq_attach_switch(TIKU_ESP32C61_LINE_SWITCH,
                                    ESP32C61_SRC_FROM_CPU0,
                                    TIKU_ESP32C61_LEVEL_SWITCH,
                                    thread_switch_isr);
    tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_SWITCH);
}

/** @brief Pend the switch (idempotent, any context). */
void tiku_thread_arch_pend(void) {
    TIKU_REG32(ESP32C61_INTPRI_FROM_CPU0) = 1UL;
    (void)TIKU_REG32(ESP32C61_INTPRI_FROM_CPU0);
}

/** @brief The core's cycle counter, for per-thread accounting. */
uint32_t tiku_thread_arch_cycles(void) {
    return ESP32C61_CSR_READ(mcycle);
}

/**
 * @brief Build a worker's first frame: the trap exit resumes it at @p entry.
 *
 * @return The frame's address, which is the worker's saved sp
 */
uint32_t *tiku_thread_arch_frame_init(uint32_t *stack_top,
                                      void (*entry)(void *),
                                      void *arg,
                                      void (*exit_fn)(void)) {
    uint32_t *sp = (uint32_t *)((uintptr_t)stack_top & ~(uintptr_t)15U);

    sp -= TIKU_ESP32C61_FRAME_WORDS;
    for (unsigned i = 0U; i < TIKU_ESP32C61_FRAME_WORDS; i++) {
        sp[i] = 0UL;
    }
    sp[TIKU_ESP32C61_F_RA]      = (uint32_t)(uintptr_t)exit_fn;
    sp[TIKU_ESP32C61_F_A0]      = (uint32_t)(uintptr_t)arg;
    sp[TIKU_ESP32C61_F_MEPC]    = (uint32_t)(uintptr_t)entry;
    sp[TIKU_ESP32C61_F_MSTATUS] = MSTATUS_MPP_M | MSTATUS_MPIE;
    sp[TIKU_ESP32C61_F_MCAUSE]  = MCAUSE_INTERRUPT | MCAUSE_MPP_M | MCAUSE_MPIE;
    return sp;
}
