/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread.h - opt-in preemptive worker threads (Cortex-M only)
 *
 * The hybrid model: thread 0 is the ENTIRE existing kernel — the
 * scheduler loop, every protothread process, every service — running
 * exactly as before, cooperative and run-to-completion.  Workers are
 * statically declared, stackful, preemptible compute threads that run
 * ONLY when the kernel has nothing to dispatch, and are preempted
 * back to the kernel the moment any event arrives (every ISR post
 * wakes thread 0, which has absolute priority).  Between themselves,
 * workers round-robin on the system tick.
 *
 * CONFINEMENT, NOT LOCKING.  Workers may do pure computation plus the
 * ISR-safe kernel primitives — tiku_process_post(), the channel API —
 * and NOTHING else: no VFS, no tier/arena, no net, no shell IO.  The
 * safety argument is structural: tiku_atomic_enter() masks PRIMASK,
 * PendSV is an interrupt, so no context switch can occur inside any
 * existing critical section — every atomic-guarded structure is
 * already worker-safe unchanged, and everything else stays
 * kernel-thread-only by policy (MPU enforcement is a later phase).
 *
 * The context switch accounts per-thread CPU cycles from the DWT
 * cycle counter at every switch — the substrate for energy-budgeted
 * scheduling (the budget field exists now; enforcement lands with the
 * energy work).
 *
 * Opt-in via TIKU_THREADS_ENABLE=1 (Makefile; Cortex-M parts only —
 * per-thread stacks are noise on 384-520 KB parts and impossible on a
 * 2 KB MSP430, which stays cooperative AND byte-identical: with the
 * flag off none of this compiles).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_THREAD_H_
#define TIKU_THREAD_H_

#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Maximum worker threads (thread 0, the kernel, is separate). */
#ifndef TIKU_THREADS_MAX
#define TIKU_THREADS_MAX        4
#endif

/** @brief Minimum worker stack size in bytes (hardware+software frame
 *  plus headroom; the FPU frame alone is 200 bytes on the M55). */
#define TIKU_THREAD_STACK_MIN   512u

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Worker thread states. */
typedef enum {
    TIKU_THREAD_UNUSED  = 0,   /**< Slot never started                    */
    TIKU_THREAD_READY   = 1,   /**< Runnable, waiting for the CPU         */
    TIKU_THREAD_RUNNING = 2,   /**< Currently on the CPU                  */
    TIKU_THREAD_DONE    = 3    /**< Exited; joinable                      */
} tiku_thread_state_t;

/**
 * @brief Thread control block.
 *
 * Statically allocated via TIKU_THREAD().  sp is the saved process
 * stack pointer while the thread is off the CPU (the arch switcher's
 * software frame lives at *sp).  cycles accumulates DWT CPU cycles
 * across every occupancy — the energy-accounting substrate; budget is
 * carried now, enforced by the energy scheduler later.
 */
typedef struct tiku_thread {
    uint32_t            *sp;          /**< Saved PSP (off-CPU)            */
    uint32_t            *stack_base;  /**< Lowest address (canary here)   */
    size_t               stack_size;  /**< Bytes                          */
    void               (*entry)(void *);
    void                *arg;
    volatile tiku_thread_state_t state;
    const char          *name;
    unsigned long long   cycles;      /**< DWT cycles consumed (total)    */
    unsigned long long   budget;      /**< Energy budget hook (unenforced)*/
    uint16_t             switches;    /**< Times scheduled onto the CPU   */
} tiku_thread_t;

/*---------------------------------------------------------------------------*/
/* DECLARATION MACRO                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_THREAD(name, stack_bytes)
 * @brief Statically declare a worker thread and its stack.
 *
 * The stack is 8-byte aligned .bss (never .persistent — high-churn
 * buffers do not belong in the NVM-mirrored section).  Start it with
 * tiku_thread_start(&name, entry, arg).
 */
#define TIKU_THREAD(name, stack_bytes)                                       \
    static uint32_t name##_stack[(stack_bytes) / 4u]                         \
        __attribute__((aligned(8)));                                        \
    tiku_thread_t name = {                                                   \
        NULL, name##_stack, (stack_bytes), NULL, NULL,                       \
        TIKU_THREAD_UNUSED, #name, 0, 0, 0                                   \
    }

/*---------------------------------------------------------------------------*/
/* WORKER API (callable from any thread)                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start a worker thread.
 *
 * First call performs the one-time bring-up: the kernel context
 * migrates from MSP to PSP (same stack, seamless), MSP is re-pointed
 * at a dedicated ISR stack, PendSV drops to the lowest exception
 * priority, and the DWT cycle counter starts.  Must be called from
 * the kernel thread (process context).
 *
 * @param t      Thread declared with TIKU_THREAD()
 * @param entry  Worker body; returning is equivalent to
 *               tiku_thread_exit()
 * @param arg    Passed to @p entry
 * @return 0 on success, -1 (bad args / stack too small / slots full /
 *         already running)
 */
int tiku_thread_start(tiku_thread_t *t, void (*entry)(void *), void *arg);

/** @brief Voluntarily give up the CPU (worker context). */
void tiku_thread_yield(void);

/** @brief Terminate the calling worker.  Never returns. */
void tiku_thread_exit(void);

/**
 * @brief Wait until @p t exits (kernel-thread context).
 *
 * Cooperative: spins yielding the CPU to workers, servicing nothing —
 * intended for tests and teardown, not steady-state code (steady
 * state should get completion via an event post instead).
 *
 * @return 0 when joined, -1 if @p t was never started
 */
int tiku_thread_join(tiku_thread_t *t);

/*---------------------------------------------------------------------------*/
/* INTROSPECTION                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Total DWT cycles @p t has consumed on the CPU. */
unsigned long long tiku_thread_cycles(const tiku_thread_t *t);

/** @brief Times @p t was scheduled onto the CPU. */
uint16_t tiku_thread_switches(const tiku_thread_t *t);

/** @brief Non-zero if any worker is READY to run. */
int tiku_thread_worker_ready(void);

/** @brief Count of stack-canary violations detected at switch time. */
uint16_t tiku_thread_canary_faults(void);

/*---------------------------------------------------------------------------*/
/* KERNEL-INTERNAL (scheduler / event-queue hooks)                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Kernel thread yields the CPU to the ready workers.
 *
 * Called from the scheduler's idle branch (interrupts masked, no
 * dispatchable work, at least one worker READY) INSTEAD of the idle
 * hook: marks thread 0 not-ready, rotates the worker round-robin, and
 * pends the switch — which fires when the atomic section exits.  The
 * kernel resumes here as soon as tiku_thread_kernel_wake() runs.
 */
void tiku_thread_kernel_block(void);

/**
 * @brief Make the kernel thread runnable again (absolute priority).
 *
 * ISR-safe; called by tiku_process_post()/poll() after every
 * successful enqueue, so ANY event — a tick's timer poll, UART RX, a
 * worker's own post — preempts workers back to the kernel at the
 * next unmasked instant.  A no-op before threading starts.
 */
void tiku_thread_kernel_wake(void);

#endif /* TIKU_THREAD_H_ */
