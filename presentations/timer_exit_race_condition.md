# Timer-Exit Race Condition in TikuOS

## Bug Report: TIKU_EVENT_EXITED Never Posted

**Found by:** TikuBench scheduler & timer edge-case test suite
**Severity:** Medium (resource leak + stale callback execution)
**Affected components:** `kernel/process/tiku_process.c`, `kernel/timers/tiku_timer.c`

---

## 1. Background

TikuOS uses an event-driven architecture with cooperative multitasking.
Processes communicate through a 32-entry circular event queue.  Software
timers are managed by a dedicated **timer process** that scans a linked
list of active timers whenever it receives a `TIKU_EVENT_POLL`.

When a timer expires, the timer process either:
- Posts `TIKU_EVENT_TIMER` to the owning process (event mode), or
- Calls the registered callback directly (callback mode).

The timer process also listens for `TIKU_EVENT_EXITED` to clean up
timers belonging to a process that has terminated:

```c
/* kernel/timers/tiku_timer.c — timer process thread */
if (ev == TIKU_EVENT_EXITED) {
    struct tiku_process *dead = data;
    /* Walk timer_list, remove timers where t->p == dead */
    ...
    continue;
}
```

---

## 2. Bug: EXITED Was Never Broadcast

`tiku_process_exit()` removed the process from the linked list and
cleared `is_running`, but **never posted `TIKU_EVENT_EXITED`**:

```c
/* BEFORE fix — kernel/process/tiku_process.c */
void tiku_process_exit(struct tiku_process *p)
{
    if (!p->is_running) return;

    p->is_running = 0;
    /* ... unlink from process list ... */

    /* <-- MISSING: no EXITED broadcast --> */
}
```

**Consequence:** The timer process's cleanup handler was dead code.
Any timer belonging to an exited process remained in the active list
and would eventually fire, calling its callback with a stale process
context pointer.

### Fix Applied

```c
/* AFTER fix — kernel/process/tiku_process.c */
void tiku_process_exit(struct tiku_process *p)
{
    if (!p->is_running) return;

    p->is_running = 0;
    /* ... unlink from process list ... */

    /* NEW: notify timer process (and others) */
    tiku_process_post(TIKU_PROCESS_BROADCAST,
                      TIKU_EVENT_EXITED,
                      (tiku_event_data_t)p);
}
```

---

## 3. Deeper Issue: The Race Condition

Even with the EXITED broadcast in place, the cleanup is **not
guaranteed to prevent the timer from firing one last time**.  This
is a fundamental consequence of the asynchronous event queue.

### Timeline of the Race

```
Time ------>

Process Thread              Event Queue              Timer Process
  |                            |                         |
  | timer_set_callback()       |                         |
  |   timer_insert()           |                         |
  |     request_poll() ------> [POLL]                    |
  |                            |                         |
  | TIKU_PROCESS_EXIT()        |                         |
  |   process_exit()           |                         |
  |     post(EXITED) --------> [POLL] [EXITED]           |
  |                            |                         |
  .                            |                         |
  .  (clock ISR ticks)         |                         |
  .                            |                         |
                               |                         |
  drain loop starts            |                         |
  process_run() ------------> dequeue [POLL]             |
                               |              ---------> scan timer_list
                               |                         timer is due!
                               |                         callback fires!
                               |                         |
  process_run() ------------> dequeue [EXITED]           |
                               |              ---------> scan timer_list
                               |                         timer already
                               |                         removed (fired)
                               |                         nothing to clean
```

### Why It Happens

1. `timer_insert()` posts `POLL` when the timer is set
2. `process_exit()` posts `EXITED` afterward
3. The queue is **FIFO** -- `POLL` is ahead of `EXITED`
4. When `POLL` is dispatched, the timer process scans for expired timers
5. If enough real time elapsed (clock ISR ticks during UART I/O), the
   timer is already due -- **it fires before EXITED is processed**
6. The EXITED handler runs next but finds nothing to clean up

### At 9600 Baud

On MSP430FR5969 with 128 Hz system clock and 9600 baud UART:

- Each UART character = 1.04 ms
- Each `TEST_PRINTF` line ~50 chars = ~52 ms = ~7 clock ticks
- Between timer set and POLL dispatch: 4-5 lines of output = ~200 ms = ~26 ticks
- Timer interval of 128 ticks (1 second) is NOT hit in this case
- But shorter intervals or longer UART output CAN trigger the race

---

## 4. Design Analysis

### Why a Synchronous Fix Is Hard

The "obvious" fix -- have `process_exit()` directly remove timers from
the timer list -- creates a **circular dependency**:

```
kernel/process/tiku_process.c
    would need to #include "kernel/timers/tiku_timer.h"
    to call a timer-removal function

kernel/timers/tiku_timer.c
    already #includes "kernel/process/tiku_process.h"
    for process_start, process_post, etc.
```

The process module is lower-level than the timer module.  Having it
reach up into the timer layer violates the dependency hierarchy:

```
         tiku_sched.c (scheduler)
              |
         tiku_timer.c (software timers)
              |
         tiku_process.c (process/event queue)
              |
         tiku_proto.h (protothreads)
```

### Possible Architectural Solutions

**Option A: Synchronous cleanup via function pointer**

Register a cleanup callback during `tiku_timer_init()`:

```c
/* In tiku_process.h */
typedef void (*tiku_exit_hook_t)(struct tiku_process *p);
void tiku_process_register_exit_hook(tiku_exit_hook_t hook);

/* In tiku_timer.c */
static void timer_cleanup_on_exit(struct tiku_process *p) {
    /* Remove all timers where t->p == p */
}

void tiku_timer_init(void) {
    ...
    tiku_process_register_exit_hook(timer_cleanup_on_exit);
}

/* In tiku_process.c */
void tiku_process_exit(struct tiku_process *p) {
    ...
    if (exit_hook) exit_hook(p);  /* synchronous cleanup */
    tiku_process_post(BROADCAST, EXITED, p);  /* async notify */
}
```

This preserves the dependency direction (timer registers into
process, not process calling into timer).

**Option B: Accept the race, document the pattern**

The EXITED handler catches "forgotten" timers.  Application code
that cares about correctness must stop its own timers before exiting:

```c
TIKU_PROCESS_THREAD(my_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_timer_set_callback(&my_timer, INTERVAL, cb, NULL);

    /* ... work ... */

    /* CORRECT: stop timer before exiting */
    tiku_timer_stop(&my_timer);
    TIKU_PROCESS_EXIT();

    TIKU_PROCESS_END();
}
```

**Current status:** Option B is implemented.  The EXITED broadcast
provides best-effort cleanup, and the test suite verifies the
explicit-stop pattern.

---

## 5. Other Bugs Found During This Investigation

### Bug 1: Premature GIE in `tiku_htimer_arch_init()`

**File:** `arch/msp430/tiku_htimer_arch.c`

`tiku_htimer_arch_init()` unconditionally called `__enable_interrupt()`
at the end of hardware timer initialization.  This enabled GIE during
`tiku_sched_init()`, before the scheduler or process system was ready.

Timer A1 compare interrupts fired immediately, posting POLL events to
a queue that wasn't expecting them.

**Fix:** Replaced `__enable_interrupt()` with `__set_interrupt_state(sr)`
to restore the caller's interrupt state.

### Bug 2: Premature GIE in boot sequence

**File:** `arch/msp430/tiku_cpu_freq_boot_arch.c`

The CPU boot function called
`tiku_cpu_boot_msp430_global_interrupts_enable()` at the end of
initialization.  This enabled GIE before the scheduler was ready.

The system clock ISR (Timer A0, configured with CCIE during boot)
started firing immediately, flooding the event queue with POLL events
during the pre-interrupt test phase.

**Fix:** Removed the `__enable_interrupt()` call from boot.  GIE is
correctly enabled later by `tiku_sched_loop()` or `test_run_all()`.

---

## 6. Test Coverage

The timer edge-case test suite (`tests/timer/test_timer_edge.c`)
now includes:

| Test | Verifies |
|------|----------|
| Timer Exit Cleanup | Explicit `timer_stop()` before exit prevents callback |
| Timer Callback Stops Other | Callback A can safely stop timer B |
| Timer Stop Inactive | `stop()` on zeroed/expired/double-stopped timer is safe |
| Timer Reset Active | Re-setting an active timer overrides the interval |
| Timer Zero Interval | Minimum interval (1 tick) fires on first poll |
| Timer Multi Expire | Two timers with same interval both fire exactly once |
| Timer Restart Now | `restart()` re-anchors to current time |
| Timer Remaining | `remaining()` decreases and reaches 0 |
| Timer Any Pending | `any_pending()` tracks timer lifecycle |

---

## 7. Recommendations

1. **Short term:** Document that processes must stop their own timers
   before exiting.  The EXITED handler is a safety net, not a guarantee.

2. **Medium term:** Implement Option A (exit hook) to provide
   synchronous timer cleanup without circular dependencies.

3. **Long term:** Consider a timer ownership model where
   `tiku_timer_set_*()` returns a handle, and the timer module tracks
   ownership internally -- enabling automatic cleanup regardless of
   exit timing.
