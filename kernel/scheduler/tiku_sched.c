/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sched.c - Scheduler implementation
 *
 * Central event-driven scheduler.  Drains the process event queue, through
 * which the timer process also runs, and calls the idle hook when no work
 * is pending.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_sched.h"
#include "kernel/memory/tiku_reclaim_internal.h"
#include "../timers/tiku_htimer.h"
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_hang.h>          /* check-in watchdog heartbeat */
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
#include <kernel/threads/tiku_thread.h>   /* worker handoff in idle */
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE VARIABLES                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Scheduler state flag */
static volatile uint8_t sched_state;

/** @brief Platform idle hook (called when no work pending) */
static tiku_sched_idle_hook_t idle_hook;

/** @brief Number of times the scheduler entered idle */
static volatile uint16_t idle_count;

/**
 * @brief Whether the registered idle mode wakes on the system tick (default 1).
 *
 * With 0 the scheduler does not idle while a software timer is armed: a mode
 * the tick cannot end would sleep past the deadline.
 */
static uint8_t idle_tick_wakes = 1;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the scheduler and all managed subsystems.
 *
 * Initialises the process system, the hardware timer and the software timers,
 * in that order: tiku_timer_init() starts a process, which needs the process
 * system in place.
 */
void tiku_sched_init(void)
{
    sched_state = TIKU_SCHED_RUNNING;

    /*
     * The idle hook defaults to TIKU_CPU_IDLE_LIGHT, the shallowest sleep
     * (LPM0 on MSP430, WFI on the other ports with an idle entry): any
     * interrupt wakes it, and peripherals and their clocks keep running.
     * With no hook the idle loop spins.  Deeper modes, which power parts
     * down, are set through tiku_power_policy_set(); "sleep off" removes
     * the hook.
     */
    idle_hook = tiku_cpu_idle_hook(TIKU_CPU_IDLE_LIGHT);
    tiku_sched_set_idle_tick_wakes(
        (uint8_t)tiku_cpu_idle_mode_wakes_on_tick(TIKU_CPU_IDLE_LIGHT));

    SCHED_PRINTF("Init: process subsystem\n");
    tiku_process_init();
    SCHED_PRINTF("Init: hardware timer\n");
    tiku_htimer_init();
    SCHED_PRINTF("Init: software timers\n");
    tiku_timer_init();
    SCHED_PRINTF("Init complete\n");
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Start a process through the scheduler.
 *
 * Wrapper around tiku_process_start() that adds debug tracing.
 */
void tiku_sched_start(struct tiku_process *p, tiku_event_data_t data)
{
    SCHED_PRINTF("Started: %s\n", p->name);
    tiku_process_start(p, data);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Run one scheduler iteration.
 *
 * Runs the memory-reclaim poll when TIKU_MEM_RECLAIM_ENABLE, then dispatches
 * one event.  It queues no timer POLL, since one on every pass keeps the loop
 * from idling; the tick and timer_insert() poll the timer process.
 *
 * @return 1 if an event was dispatched, 0 if idle
 */
uint8_t tiku_sched_run_once(void)
{
#if TIKU_MEM_RECLAIM_ENABLE
    tiku_mem_reclaim_poll();
#endif
    return tiku_process_run();
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Main scheduler loop.
 *
 * Drain every pending event, then idle until an interrupt wakes the CPU.  The
 * idle hook runs inside an atomic section so no interrupt is lost between the
 * "is there work?" check and the low-power entry, which the HAL makes atomic.
 */
void tiku_sched_loop(void)
{
    SCHED_PRINTF("Entering scheduler loop\n");

#if TIKU_AUTOSTART_ENABLE
    tiku_autostart_start(tiku_autostart_processes);
#endif

    /* Enable global interrupts so ISRs (timer tick, UART RX, etc.)
     * can fire.  The scheduler's idle path uses atomic enter/exit, which
     * restores the interrupt-enable state (GIE, PRIMASK or mstatus.MIE), so
     * once enabled here it stays on. */
    tiku_cpu_irq_enable();

    /* Arm the check-in hang watchdog: from here a tick ISR that calls
     * tiku_hang_tick() watches for a process that wedges this loop.  This is
     * the only arming call, so code that never enters the loop never trips
     * it. */
    tiku_hang_arm();

    while (sched_state == TIKU_SCHED_RUNNING) {

        /* Drain all pending work.  The heartbeat advances once per dispatched
         * event; a process that wedges inside run_once() never lets it turn,
         * which is what the tick-ISR hang detector watches for. */
        while (tiku_sched_run_once()) {
            tiku_hang_checkin();
        }

        /*
         * No more events — enter idle.
         *
         * The atomic section ensures that an ISR firing between the
         * check and the idle hook has its event processed on the next
         * iteration, not slept through.
         *
         * An armed (not yet due) timer does not block idle when the
         * registered idle mode is tick-woken: the tick ISR wakes the
         * CPU, posts the timer poll, and the next loop pass
         * dispatches it (the MSP430 tick ISR clears the LPM bits on
         * exit; elsewhere the tick interrupt ends the sleep).  Only
         * when the idle mode's wake set excludes the tick
         * (idle_tick_wakes == 0, e.g. MSP430 LPM4) do armed timers
         * keep the CPU awake — sleeping would miss the deadline forever.
         */
        tiku_atomic_enter();

        if (!tiku_sched_has_pending() &&
            (idle_tick_wakes || (!tiku_timer_any_pending()
#if TIKU_MEM_RECLAIM_ENABLE
                                && !tiku_mem_reclaim_pending()
#endif
                                ))) {
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
            if (tiku_thread_worker_ready()) {
                /* Not idle — hand the CPU to the ready workers.  The
                 * switch fires at the atomic exit below; the kernel
                 * resumes right there when an event post or the tick
                 * wakes it (the tick at the latest, which also
                 * time-slices the workers).  idle_count is not
                 * bumped: running workers is work, not idle. */
                tiku_thread_kernel_block();
            } else
#endif
            {
                uint8_t stretched = 0u;

                /* Tickless: with timers armed (none due -- has_pending
                 * said so) and a tick-woken sleep registered, the arch
                 * may stretch the next tick interrupt to the earliest
                 * deadline (the weak default never does).  IRQs are
                 * masked, so no deadline moves meanwhile.  With no timer
                 * armed the tick keeps its per-tick cadence. */
                if (idle_tick_wakes &&
#if TIKU_MEM_RECLAIM_ENABLE
                    !tiku_mem_reclaim_pending() &&
#endif
                    idle_hook != (tiku_sched_idle_hook_t)0 &&
                    tiku_timer_any_pending()) {
                    tiku_clock_time_t ahead = (tiku_clock_time_t)
                        (tiku_timer_next_expiration() - tiku_clock_time());
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
                    unsigned long at;

                    /* A blocked worker's wait deadline counts as a timer:
                     * stretching past it would make its timeout late. */
                    if (tiku_thread_next_deadline(&at)) {
                        tiku_clock_time_t t = (tiku_clock_time_t)
                            ((tiku_clock_time_t)at - tiku_clock_time());
                        if (t < ahead) {
                            ahead = t;
                        }
                    }
#endif
                    if (ahead > 1u) {
                        stretched =
                            (uint8_t)tiku_clock_tickless_begin(ahead);
                    }
                }

                idle_count++;
                if (idle_hook != (tiku_sched_idle_hook_t)0) {
                    idle_hook();
                }
                if (stretched) {
                    tiku_clock_tickless_end();
                }
            }
        }

        tiku_atomic_exit();
    }
}

/*---------------------------------------------------------------------------*/

/** @brief Signal the scheduler to stop its main loop. */
void tiku_sched_stop(void)
{
    SCHED_PRINTF("Stopped\n");
    sched_state = TIKU_SCHED_STOPPED;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Check if the scheduler has dispatchable work right now.
 *
 * Non-zero when the event queue is non-empty or a software timer is due.  A
 * timer merely armed for a future deadline is not pending work -- the tick ISR
 * posts its poll when the deadline arrives.
 */
uint8_t tiku_sched_has_pending(void)
{
    if (!tiku_process_queue_empty()) {
        return 1;
    }

    if (tiku_timer_work_pending()) {
        return 1;
    }

    return 0;
}

/*---------------------------------------------------------------------------*/

/** @brief Register a callback invoked when the scheduler is idle. */
void tiku_sched_set_idle_hook(tiku_sched_idle_hook_t hook)
{
    idle_hook = hook;
}

/** @brief Get the idle hook the scheduler calls. */
tiku_sched_idle_hook_t tiku_sched_get_idle_hook(void)
{
    return idle_hook;
}

/** @brief Declare whether the registered idle mode wakes on the tick. */
void tiku_sched_set_idle_tick_wakes(uint8_t wakes)
{
    idle_tick_wakes = wakes ? 1u : 0u;
}

/*---------------------------------------------------------------------------*/

/** @brief Return the number of idle entries since boot. */
uint16_t tiku_sched_idle_count(void)
{
    return idle_count;
}

/**
 * @brief The tick: wake the timer process if a timer exists, and give
 *        the kernel thread its turn.
 *
 * With workers, the tick is preemption: a spinning worker is displaced
 * only because the kernel thread is woken here, whether or not a timer
 * exists.
 */
void tiku_sched_notify(void)
{
    tiku_timer_request_poll();
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
    tiku_thread_kernel_wake();
#endif
}

/*---------------------------------------------------------------------------*/
