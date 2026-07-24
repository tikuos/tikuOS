/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/**
 * @brief Is worker @p t eligible for the CPU right now?
 *
 * READY and within its energy budget.  budget == 0 is unlimited (the
 * default — every worker that never set a budget behaves exactly as
 * before); otherwise the worker is eligible only while its accounted
 * cycles stay below the ceiling.  This is the single enforcement point:
 * both the switcher's pick loop and tiku_thread_worker_ready() consult
 * it, so the switch and the scheduler always agree on who may run, and
 * an over-budget worker is invisible to both until it is refilled.
 */
static int worker_runnable(const tiku_thread_t *t)
{
    return t != (const tiku_thread_t *)0 &&
           t->state == TIKU_THREAD_READY &&
           (t->budget == 0ull || t->cycles < t->budget);
}

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
/**
 * @brief Non-zero when the caller runs in kernel-thread (or pre-thread
 *        boot) context; zero inside a preemptive worker.
 *
 * The confinement predicate for TIKU_MEM_KERNEL_ONLY (kernel/memory):
 * memory mutators refuse worker-context calls instead of racing the
 * cooperative kernel's lock-free structures.  A single aligned pointer
 * read of s_current (already volatile) — safe from any context; an ISR
 * that interrupted a worker reads "not kernel", which is the correct
 * conservative answer for allocators there too.
 */
int tiku_thread_in_kernel(void)
{
    return !s_started || s_current == (tiku_thread_t *)0;
}

/**
 * @brief Cooperative context-switch core: park the outgoing context and pick
 *        the next worker to run.
 *
 * Invoked from the arch switch trampoline with the outgoing stack pointer.
 * Charges the elapsed cycles to whoever was running (a worker or the kernel),
 * saves its @p old_sp, checks the outgoing stack canary, then selects the next
 * READY, in-budget worker from the round-robin cursor (an over-budget worker
 * is skipped until a refill lifts it back over budget).  With no runnable
 * worker it returns to the kernel context.
 *
 * @param old_sp  Stack pointer of the context being switched out.
 * @return The stack pointer of the context to switch in.
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

    /* Pick the incoming context: the next READY, in-budget worker from
     * the round-robin cursor.  An exhausted worker is skipped here, so it
     * silently loses its turn until a refill lifts it back over budget. */
    if (!s_kernel_ready) {
        for (i = 0; i < TIKU_THREADS_MAX; i++) {
            uint8_t idx = (uint8_t)((s_rr + i) % TIKU_THREADS_MAX);
            if (worker_runnable(s_threads[idx])) {
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
        if (worker_runnable(s_threads[i])) {
            return 1;
        }
    }
    return 0;
}

uint8_t tiku_thread_count(void)
{
    /* The slot capacity to iterate; tiku_thread_get() returns NULL for an
     * empty slot, so callers skip those. */
    return (uint8_t)TIKU_THREADS_MAX;
}

tiku_thread_t *tiku_thread_get(uint8_t i)
{
    return (i < (uint8_t)TIKU_THREADS_MAX) ? s_threads[i] : (tiku_thread_t *)0;
}

tiku_thread_state_t tiku_thread_state(const tiku_thread_t *t)
{
    return (t != (const tiku_thread_t *)0) ? t->state : TIKU_THREAD_DONE;
}

int tiku_thread_is_done(const tiku_thread_t *t)
{
    /* A NULL/never-started worker reads as done so an await can't hang. */
    return (t == (const tiku_thread_t *)0) || (t->state == TIKU_THREAD_DONE);
}

/*---------------------------------------------------------------------------*/
/* ENERGY BUDGET                                                             */
/*---------------------------------------------------------------------------*/

/*
 * budget is a 64-bit field the PendSV switcher reads while picking the
 * next worker; a two-store update could be torn by the switch, so every
 * mutation runs under the PRIMASK atomic section (PendSV is an interrupt
 * and cannot fire while it is masked).  cycles is written only by the
 * switch itself, so the comparisons never race it.
 */

void tiku_thread_budget_grant(tiku_thread_t *t, unsigned long long cycles)
{
    if (t == (tiku_thread_t *)0) {
        return;
    }
    tiku_atomic_enter();
    /* Ceiling = already-consumed + allowance.  Guard the one aliasing
     * corner: a never-run worker (cycles == 0) granted 0 would land on
     * budget == 0 and read as "unlimited" — force it to the parked side. */
    t->budget = t->cycles + cycles;
    if (t->budget == 0ull) {
        t->budget = 1ull;
    }
    tiku_atomic_exit();
}

void tiku_thread_budget_refill(tiku_thread_t *t, unsigned long long cycles)
{
    if (t == (tiku_thread_t *)0) {
        return;
    }
    tiku_atomic_enter();
    if (t->budget != 0ull) {          /* only extend an already-enforced budget */
        t->budget += cycles;
    }
    tiku_atomic_exit();
}

void tiku_thread_budget_unlimited(tiku_thread_t *t)
{
    if (t == (tiku_thread_t *)0) {
        return;
    }
    tiku_atomic_enter();
    t->budget = 0ull;
    tiku_atomic_exit();
}

unsigned long long tiku_thread_budget_remaining(const tiku_thread_t *t)
{
    unsigned long long rem;
    if (t == (const tiku_thread_t *)0) {
        return ~0ull;
    }
    tiku_atomic_enter();
    rem = (t->budget == 0ull)
        ? ~0ull
        : ((t->cycles < t->budget) ? (t->budget - t->cycles) : 0ull);
    tiku_atomic_exit();
    return rem;
}

int tiku_thread_budget_exhausted(const tiku_thread_t *t)
{
    int ex;
    if (t == (const tiku_thread_t *)0) {
        return 0;
    }
    tiku_atomic_enter();
    ex = (t->budget != 0ull && t->cycles >= t->budget);
    tiku_atomic_exit();
    return ex;
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
