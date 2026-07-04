/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread.c - preemptive worker threads: portable core
 *
 * Policy lives here; the mechanics (PendSV switcher, frame layout,
 * MSP->PSP migration, DWT) live in the arch backend.  The scheduling
 * policy is deliberately minimal:
 *
 *   - Thread 0 (the kernel) has ABSOLUTE priority: whenever it is
 *     runnable it gets the CPU.  It goes not-runnable only in the
 *     scheduler's idle branch via tiku_thread_kernel_block(), and any
 *     successful event post makes it runnable again.
 *   - Workers round-robin the CPU the kernel leaves behind.  The
 *     rotation advances every time the kernel blocks and on every
 *     voluntary yield; since the system tick posts the timer poll
 *     each tick (waking the kernel), workers are naturally
 *     time-sliced at tick granularity.
 *   - If nothing is runnable the switcher falls back to the kernel:
 *     its loop is the only context that knows how to idle properly
 *     (tickless stretch + WFI), so the "all quiet" case lands there.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_thread.h"
#include <hal/tiku_cpu.h>            /* tiku_atomic_enter/exit */

/*---------------------------------------------------------------------------*/
/* ARCH BACKEND INTERFACE (arch/<family>/tiku_thread_arch.c)                 */
/*---------------------------------------------------------------------------*/

/** One-time bring-up: ISR stack, MSP->PSP migration, PendSV priority,
 *  DWT cycle counter.  Runs in kernel (thread) context. */
extern void      tiku_thread_arch_boot(void);
/** Pend the context-switch exception (ISR-safe, idempotent). */
extern void      tiku_thread_arch_pend(void);
/** Free-running CPU cycle counter (wraps; deltas are what matter). */
extern uint32_t  tiku_thread_arch_cycles(void);
/** Build a worker's initial exception frame; returns the initial sp. */
extern uint32_t *tiku_thread_arch_frame_init(uint32_t *stack_top,
                                             void (*entry)(void *),
                                             void *arg,
                                             void (*exit_fn)(void));

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Stack-canary magic stamped at every worker's stack base. */
#define THREAD_CANARY  0xC0FFEE55u

/** @brief Registered workers (slot order = round-robin order). */
static tiku_thread_t *s_threads[TIKU_THREADS_MAX];

/** @brief Currently running worker, or NULL when thread 0 (kernel)
 *  owns the CPU.  Written only inside the PendSV switcher. */
static tiku_thread_t * volatile s_current;

/** @brief Kernel thread runnable?  Cleared by kernel_block, set by
 *  kernel_wake (any event post).  volatile: ISRs write it. */
static volatile uint8_t s_kernel_ready = 1;

/** @brief Round-robin cursor into s_threads[]. */
static uint8_t s_rr;

/** @brief One-time arch bring-up done (first tiku_thread_start). */
static uint8_t s_started;

/** @brief Kernel thread's saved sp while a worker runs. */
static uint32_t *s_kernel_sp;

/** @brief Kernel thread cycle account (thread 0's share). */
static unsigned long long s_kernel_cycles;

/** @brief DWT snapshot at the last switch (start of current tenure). */
static uint32_t s_tenure_start;

/** @brief Stack-canary violations seen at switch time. */
static volatile uint16_t s_canary_faults;

/*---------------------------------------------------------------------------*/
/* THE SWITCH (called from the PendSV switcher with IRQs implicitly          */
/* serialised — PendSV is the lowest-priority exception)                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Save the outgoing context's sp, account its cycles, pick next.
 *
 * Called by the arch PendSV handler with the outgoing thread's sp
 * (software frame already pushed).  Returns the incoming thread's sp
 * (whose software frame the handler pops).  Policy: kernel if
 * runnable; else the next READY worker from the round-robin cursor;
 * else the kernel as fallback (it knows how to idle).
 *
 * @param old_sp  Outgoing stack pointer (past the software frame)
 * @return Incoming stack pointer
 */
uint32_t *tiku_thread_switch(uint32_t *old_sp)
{
    uint32_t now = tiku_thread_arch_cycles();
    unsigned long long tenure = (uint32_t)(now - s_tenure_start);
    tiku_thread_t *next = (tiku_thread_t *)0;
    uint8_t i;

    /* Account + park the outgoing context. */
    if (s_current == (tiku_thread_t *)0) {
        s_kernel_cycles += tenure;
        s_kernel_sp = old_sp;
    } else {
        s_current->cycles += tenure;
        s_current->sp = old_sp;
        if (s_current->state == TIKU_THREAD_RUNNING) {
            s_current->state = TIKU_THREAD_READY;
        }
        if (s_current->stack_base[0] != THREAD_CANARY) {
            s_canary_faults++;
        }
    }

    /* Pick the incoming context. */
    if (!s_kernel_ready) {
        for (i = 0; i < TIKU_THREADS_MAX; i++) {
            uint8_t idx = (uint8_t)((s_rr + i) % TIKU_THREADS_MAX);
            if (s_threads[idx] != (tiku_thread_t *)0 &&
                s_threads[idx]->state == TIKU_THREAD_READY) {
                next = s_threads[idx];
                break;
            }
        }
    }

    s_tenure_start = tiku_thread_arch_cycles();

    if (next == (tiku_thread_t *)0) {
        /* Kernel: by priority, or as the idle-capable fallback. */
        s_current = (tiku_thread_t *)0;
        return s_kernel_sp;
    }

    next->state = TIKU_THREAD_RUNNING;
    next->switches++;
    s_current = next;
    return next->sp;
}

/*---------------------------------------------------------------------------*/
/* WORKER API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_thread_start(tiku_thread_t *t, void (*entry)(void *), void *arg)
{
    uint8_t i;
    int slot = -1;

    if (t == (tiku_thread_t *)0 || entry == (void (*)(void *))0 ||
        t->stack_size < TIKU_THREAD_STACK_MIN ||
        t->state == TIKU_THREAD_READY || t->state == TIKU_THREAD_RUNNING) {
        return -1;
    }

    if (!s_started) {
        tiku_thread_arch_boot();
        s_tenure_start = tiku_thread_arch_cycles();
        s_started = 1;
    }

    tiku_atomic_enter();

    for (i = 0; i < TIKU_THREADS_MAX; i++) {
        if (s_threads[i] == t) { slot = (int)i; break; }   /* restart */
        /* A DONE occupant has given the CPU back for good: its slot
         * is reclaimable (the TCB itself stays valid for join()). */
        if (slot < 0 &&
            (s_threads[i] == (tiku_thread_t *)0 ||
             s_threads[i]->state == TIKU_THREAD_DONE)) {
            slot = (int)i;
        }
    }
    if (slot < 0) {
        tiku_atomic_exit();
        return -1;
    }
    s_threads[slot] = t;

    t->entry = entry;
    t->arg   = arg;
    t->stack_base[0] = THREAD_CANARY;
    t->sp = tiku_thread_arch_frame_init(
        t->stack_base + (t->stack_size / 4u), entry, arg, tiku_thread_exit);
    t->state = TIKU_THREAD_READY;

    tiku_atomic_exit();
    return 0;
}

void tiku_thread_yield(void)
{
    tiku_atomic_enter();
    s_rr = (uint8_t)((s_rr + 1u) % TIKU_THREADS_MAX);
    tiku_atomic_exit();
    tiku_thread_arch_pend();
}

void tiku_thread_exit(void)
{
    tiku_thread_t *self = s_current;

    if (self != (tiku_thread_t *)0) {
        self->state = TIKU_THREAD_DONE;
    }
    /* Give the CPU back for good; the kernel (or the next worker)
     * takes over at the pended switch.  Never returns. */
    for (;;) {
        tiku_thread_arch_pend();
    }
}

int tiku_thread_join(tiku_thread_t *t)
{
    if (t == (tiku_thread_t *)0 || t->state == TIKU_THREAD_UNUSED) {
        return -1;
    }
    /* Kernel-context wait: repeatedly hand the CPU to the workers.
     * The system tick's poll post wakes the kernel every tick, so
     * this loop re-checks at tick granularity.  Test/teardown tool —
     * steady-state code should take a completion event instead. */
    while (t->state != TIKU_THREAD_DONE) {
        tiku_atomic_enter();
        if (tiku_thread_worker_ready()) {
            tiku_thread_kernel_block();
        }
        tiku_atomic_exit();
        /* PendSV fires here (if pended); kernel resumes on wake. */
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* INTROSPECTION                                                             */
/*---------------------------------------------------------------------------*/

unsigned long long tiku_thread_cycles(const tiku_thread_t *t)
{
    return (t != (const tiku_thread_t *)0) ? t->cycles : s_kernel_cycles;
}

uint16_t tiku_thread_switches(const tiku_thread_t *t)
{
    return (t != (const tiku_thread_t *)0) ? t->switches : 0u;
}

int tiku_thread_worker_ready(void)
{
    uint8_t i;
    for (i = 0; i < TIKU_THREADS_MAX; i++) {
        if (s_threads[i] != (tiku_thread_t *)0 &&
            s_threads[i]->state == TIKU_THREAD_READY) {
            return 1;
        }
    }
    return 0;
}

uint16_t tiku_thread_canary_faults(void)
{
    return s_canary_faults;
}

/*---------------------------------------------------------------------------*/
/* KERNEL HOOKS                                                              */
/*---------------------------------------------------------------------------*/

void tiku_thread_kernel_block(void)
{
    if (!s_started) {
        return;
    }
    s_kernel_ready = 0;
    s_rr = (uint8_t)((s_rr + 1u) % TIKU_THREADS_MAX);
    tiku_thread_arch_pend();
    /* Caller holds the atomic section; the switch fires at its exit
     * and the kernel resumes right there once kernel_wake runs. */
}

void tiku_thread_kernel_wake(void)
{
    if (!s_started || s_kernel_ready) {
        return;
    }
    s_kernel_ready = 1;
    tiku_thread_arch_pend();
}
