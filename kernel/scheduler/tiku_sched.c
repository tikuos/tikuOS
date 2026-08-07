/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sched.c - Scheduler implementation
 *
 * Central event-driven scheduler. Drains the process event queue,
 * services expired software timers, and enters a low-power idle
 * state when no work is pending.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_sched.h"
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
 * @brief Whether the registered idle mode is woken by the system tick.
 *
 * Gates idling while software timers are armed but not yet due.  A tick-woken
 * mode is safe to sleep in; a mode whose wake set excludes the tick would sleep
 * past the deadline, so the scheduler stays awake instead.  Defaults to 1.
 */
static uint8_t idle_tick_wakes = 1;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the scheduler and all managed subsystems.
 *
 * Order matters: the process system first, then the hardware timer, then the
 * software timers, whose management process the last step starts.
 */
void tiku_sched_init(void)
{
    sched_state = TIKU_SCHED_RUNNING;

    /*
     * Idle sleeps by default.  With no hook installed an idle scheduler spins
     * the core at full speed -- measured on an nRF54LM20-DK at 6.76 mA doing
     * nothing against 1.54 mA in WFI, a factor of 4.4 paid continuously by
     * every board that never calls "sleep".
     *
     * The cost of spinning is not only the CPU: on this part the HFCLK
     * controller stops the clock automatically once nothing requests it, and
     * "the CPU is awake" is itself a request.  Never sleeping therefore holds
     * the whole MCU power domain up, and the hardware's own power management
     * never gets a chance to act.
     *
     * TIKU_CPU_IDLE_LIGHT is the conservative choice: a plain WFI that any
     * interrupt wakes, with no clock or peripheral state torn down, so nothing
     * that worked while spinning can stop working.  Deeper modes stay opt-in
     * because they do change what remains powered.  "sleep off" restores the
     * spin for anyone who needs it -- a tight-latency experiment, or bisecting
     * a fault that only appears when the core never sleeps.
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
 * Dispatches one event.  Timer polling belongs to the clock ISR and to
 * timer_insert(); polling here would flood the queue with redundant POLL events
 * and drop real ones once it filled.
 *
 * @return 1 if an event was dispatched, 0 if idle
 */
uint8_t tiku_sched_run_once(void)
{
    /* Dispatch one event from the queue */
    return tiku_process_run();
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Main scheduler loop.
 *
 * Drain every pending event, then idle until an interrupt wakes the CPU.  The
 * idle hook runs inside an atomic section so no interrupt is lost between the
 * "is there work?" check and the low-power entry, which the HAL enters atomically.
 */
void tiku_sched_loop(void)
{
    SCHED_PRINTF("Entering scheduler loop\n");

#if TIKU_AUTOSTART_ENABLE
    tiku_autostart_start(tiku_autostart_processes);
#endif

    /* Enable global interrupts so ISRs (timer tick, UART RX, etc.)
     * can fire.  The scheduler's idle path uses atomic enter/exit
     * which preserves GIE state, so once enabled here it stays on. */
    tiku_cpu_irq_enable();

    /* Arm the check-in hang watchdog: from here the tick ISR watches for a
     * process that wedges this loop.  (Only here -- a test harness that never
     * enters this loop leaves the detector dormant.) */
    tiku_hang_arm();

    while (sched_state == TIKU_SCHED_RUNNING) {

        /* Drain all pending work.  The heartbeat advances once per dispatched
         * event; a process that wedges inside run_once() never lets it turn,
         * which is exactly what the tick-ISR hang detector watches for. */
        while (tiku_sched_run_once()) {
            tiku_hang_checkin();
        }

        /*
         * No more events — enter idle.
         *
         * The atomic section ensures that an ISR firing between the
         * check and the idle hook has its event processed on the next
         * iteration rather than missed during sleep.
         *
         * An ARMED (not yet due) timer does not block idle when the
         * registered idle mode is tick-woken: the tick ISR wakes the
         * CPU, posts the timer poll, and the next loop pass
         * dispatches it (the MSP430 tick ISR clears the LPM bits on
         * exit; the Cortex-M modes are plain WFI).  Only when the
         * idle mode's wake set excludes the tick (idle_tick_wakes
         * == 0, e.g. MSP430 LPM4) do armed timers keep the CPU
         * awake — sleeping would miss the deadline forever.
         */
        tiku_atomic_enter();

        if (!tiku_sched_has_pending() &&
            (idle_tick_wakes || !tiku_timer_any_pending())) {
#if defined(TIKU_THREADS_ENABLE) && TIKU_THREADS_ENABLE
            if (tiku_thread_worker_ready()) {
                /* Not idle — hand the CPU to the ready workers.  The
                 * switch fires at the atomic exit below; the kernel
                 * resumes right there when any event post wakes it
                 * (the tick's timer poll at the latest, which also
                 * time-slices the workers).  idle_count is NOT
                 * bumped: running workers is work, not idle. */
                tiku_thread_kernel_block();
            } else
#endif
            {
                uint8_t stretched = 0u;

                /* Tickless: with timers armed (none due — has_pending
                 * said so) and a tick-woken sleep registered, ask the
                 * arch to stretch the next tick interrupt straight to
                 * the earliest deadline instead of waking every tick
                 * to do nothing.  IRQs are masked, so the deadline
                 * cannot move.  The weak default never stretches, and
                 * with NO timers armed the per-tick cadence is kept
                 * (it is what paces uptime and the counted-idle tests). */
                if (idle_tick_wakes &&
                    idle_hook != (tiku_sched_idle_hook_t)0 &&
                    tiku_timer_any_pending()) {
                    tiku_clock_time_t ahead = (tiku_clock_time_t)
                        (tiku_timer_next_expiration() - tiku_clock_time());
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
    /* Work is pending if the process event queue is non-empty
     * or if any software timer is DUE (not merely armed). */
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

/** @brief Wake the timer management process to check for expired timers. */
void tiku_sched_notify(void)
{
    tiku_timer_request_poll();
}

/*---------------------------------------------------------------------------*/
