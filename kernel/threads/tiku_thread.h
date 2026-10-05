/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread.h - opt-in preemptive worker threads (not on MSP430, STM32N6).
 *
 * Thread 0 is the cooperative kernel; workers are statically declared,
 * preemptible compute threads that run only when it has nothing to dispatch.
 * A worker may compute and post events, and nothing else.
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

struct tiku_process;

/** @brief Worker thread states. */
typedef enum {
    TIKU_THREAD_UNUSED  = 0,   /**< Slot never started                    */
    TIKU_THREAD_READY   = 1,   /**< Runnable, waiting for the CPU         */
    TIKU_THREAD_RUNNING = 2,   /**< Currently on the CPU                  */
    TIKU_THREAD_DONE    = 3,   /**< Exited; joinable                      */
    TIKU_THREAD_BLOCKED = 4    /**< In tiku_thread_wait(): off the CPU    */
} tiku_thread_state_t;

/**
 * @brief Thread control block.
 *
 * Statically allocated via TIKU_THREAD().  sp holds the saved process stack
 * pointer while the thread is off the CPU; cycles accumulates CPU cycles across
 * every occupancy, and budget is the enforced ceiling plus one (0 = none).
 */
typedef struct tiku_thread {
    uint32_t            *sp;          /**< Saved PSP (off-CPU)            */
    uint32_t            *stack_base;  /**< Lowest address (canary here)   */
    size_t               stack_size;  /**< Bytes                          */
    void               (*entry)(void *);
    void                *arg;
    volatile tiku_thread_state_t state;
    const char          *name;
    unsigned long long   cycles;      /**< CPU cycles consumed (total)    */
    unsigned long long   budget;      /**< Ceiling + 1; 0 = unlimited     */
    uint16_t             switches;    /**< Times scheduled onto the CPU   */
    uint8_t              slot;        /**< Index in the scheduler's table */
    uint8_t              timed;       /**< A wait with a deadline         */
    unsigned long        wake_at;     /**< That deadline, in clock ticks  */
    struct tiku_process *waiter;      /**< Polled when the worker exits   */
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
 * The first call performs the one-time bring-up; on Cortex-M the kernel
 * context migrates from MSP to PSP on the same stack, MSP re-points at a
 * dedicated ISR stack, and PendSV drops to the lowest priority.
 *
 * @param t      Thread declared with TIKU_THREAD()
 * @param entry  Worker body; returning is equivalent to tiku_thread_exit()
 * @param arg    Passed to @p entry
 * @return 0 on success, -1 (bad args / stack too small / slots full /
 *         already running)
 * @note Kernel thread only.
 */
int tiku_thread_start(tiku_thread_t *t, void (*entry)(void *), void *arg);

/** @brief Voluntarily give up the CPU (worker context). */
void tiku_thread_yield(void);

/** @brief Terminate the calling worker.  Never returns. */
void tiku_thread_exit(void);

/**
 * @brief Forget a finished worker: its slot empties, so its control block and
 *        stack may be freed or reused.
 * @return 0, or -1 while @p t has not finished
 * @note Forget a worker before its memory goes away.
 */
int tiku_thread_forget(tiku_thread_t *t);

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
/* WAIT QUEUES (any thread, the kernel's included; wake from anywhere)       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Who is blocked on one object: a bit per worker slot, and bit 7 for
 *        the kernel thread.  Zero-initialised is empty.
 */
typedef struct {
    volatile uint8_t waiters;
} tiku_waitq_t;

/** @brief The kernel thread's bit in tiku_waitq_t.waiters. */
#define TIKU_WAITQ_KERNEL   0x80u

/**
 * @brief Block the caller on @p q until a wake, or for @p ticks (0: no limit).
 *
 * The kernel thread also returns on any event post.  A worker sleeps off the
 * CPU; the kernel hands the CPU to ready workers, else idles in the CPU's
 * wait.
 *
 * @return 0 once the deadline passed, 1 otherwise
 * @note Call inside exactly one tiku_atomic_enter(), with the condition just
 *       found false, and test it again on return.
 */
int tiku_thread_wait(tiku_waitq_t *q, unsigned long ticks);

/** @brief Wake one waiter, workers before the kernel; safe from an ISR. */
void tiku_thread_wake_one(tiku_waitq_t *q);

/** @brief Wake every waiter; safe from an ISR. */
void tiku_thread_wake_all(tiku_waitq_t *q);

/*---------------------------------------------------------------------------*/
/* INTROSPECTION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Total cycles @p t has consumed on the CPU (DWT, or the port's cycle
 *        counter); NULL reads the kernel thread's share.
 */
unsigned long long tiku_thread_cycles(const tiku_thread_t *t);

/** @brief Times @p t was scheduled onto the CPU. */
uint16_t tiku_thread_switches(const tiku_thread_t *t);

/** @brief Non-zero if any worker is READY to run. */
int tiku_thread_worker_ready(void);

/** @brief The worker on the CPU, or NULL in the kernel thread (an ISR sees
 *         whoever it interrupted). */
tiku_thread_t *tiku_thread_self(void);

/**
 * @brief The nearest deadline a blocked worker waits for, so a tickless idle
 *        does not sleep past it.  @return 1 with @p at set, 0 when none waits
 */
int tiku_thread_next_deadline(unsigned long *at);

/** @brief Count of stack-canary violations detected at switch time. */
uint16_t tiku_thread_canary_faults(void);

/** @brief Non-zero in kernel/boot context, zero in a worker (any context). */
int tiku_thread_in_kernel(void);

/**
 * @brief Number of worker slots to iterate (TIKU_THREADS_MAX); an empty one
 *        reads NULL from tiku_thread_get().
 */
uint8_t tiku_thread_count(void);

/** @brief The i-th registered worker (0..count-1), or NULL. */
tiku_thread_t *tiku_thread_get(uint8_t i);

/** @brief Current state of @p t (READY / RUNNING / DONE / ...). */
tiku_thread_state_t tiku_thread_state(const tiku_thread_t *t);

/** @brief Non-zero once @p t has finished, and for a worker never started. */
int tiku_thread_is_done(const tiku_thread_t *t);

/**
 * @brief The condition TIKU_WAIT_WORKER() tests: non-zero once @p t is done;
 *        until then @p p is the process polled when @p t exits.
 * @note One waiter per worker: the latest call names the process polled.
 */
int tiku_thread_await(tiku_thread_t *t, struct tiku_process *p);

/**
 * @brief Park the calling process until worker @p t finishes.
 *
 * Blocks only while @p t is unfinished; the worker's exit polls the process,
 * so it resumes with no other event.  Use inside a TIKU_PROCESS_THREAD -- code
 * running mid C-callstack cannot yield and has to keep driving a pump instead.
 */
#define TIKU_WAIT_WORKER(t) \
    PT_WAIT_UNTIL(process_pt, tiku_thread_await((t), TIKU_THIS()))

/*---------------------------------------------------------------------------*/
/* ENERGY BUDGET (cycle-quota enforcement)                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Grant @p t @p cycles of CPU run-time starting now.
 *
 * Sets a cumulative ceiling at already-consumed plus @p cycles, parking the
 * worker when it is reached until a refill.  Enforcement is at switch
 * boundaries, so a worker overruns by at most one tenure.  Granting 0 parks it.
 */
void tiku_thread_budget_grant(tiku_thread_t *t, unsigned long long cycles);

/**
 * @brief Add @p cycles to @p t's ceiling (refill / periodic top-up).
 *
 * Extends the runway; if the worker was exhausted this re-enables it for
 * @p cycles more.  A no-op on an unlimited (budget == 0) worker: grant a
 * budget first to begin enforcing.
 */
void tiku_thread_budget_refill(tiku_thread_t *t, unsigned long long cycles);

/** @brief Clear @p t's budget: unlimited, never parked for cycles (default). */
void tiku_thread_budget_unlimited(tiku_thread_t *t);

/** @brief Cycles @p t may still run before exhaustion (~0 if unlimited). */
unsigned long long tiku_thread_budget_remaining(const tiku_thread_t *t);

/** @brief Non-zero if @p t is budgeted and has spent its allowance. */
int tiku_thread_budget_exhausted(const tiku_thread_t *t);

/*---------------------------------------------------------------------------*/
/* KERNEL-INTERNAL (scheduler / event-queue hooks)                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Kernel thread yields the CPU to the ready workers.
 *
 * Called from the scheduler's idle branch instead of the idle hook: marks
 * thread 0 not-ready, rotates the worker cursor and pends the switch, which
 * fires when the atomic section exits.  It resumes on kernel_wake().
 */
void tiku_thread_kernel_block(void);

/**
 * @brief Make the kernel thread runnable again (absolute priority).
 *
 * ISR-safe, and called after every successful post, so any event preempts
 * workers back to the kernel at the next unmasked instant.  A no-op before
 * threading has started.
 */
void tiku_thread_kernel_wake(void);

#endif /* TIKU_THREAD_H_ */
