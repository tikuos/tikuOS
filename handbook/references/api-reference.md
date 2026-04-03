# TikuOS Core API Reference

**Version 0.01**

This document covers the core kernel API surface. All declarations live in the
headers listed below; include `tiku.h` to pull in the full platform
configuration, then include individual module headers as needed.

---

## Table of Contents

- [Process Management](#process-management) (`kernel/process/tiku_process.h`)
- [Protothreads](#protothreads) (`kernel/process/tiku_proto.h`)
- [Local Continuations](#local-continuations) (`kernel/process/tiku_lc.h`)
- [Software Timers](#software-timers) (`kernel/timers/tiku_timer.h`)
- [System Clock](#system-clock) (`kernel/timers/tiku_clock.h`)
- [Hardware Timer](#hardware-timer) (`kernel/timers/tiku_htimer.h`)
- [Scheduler](#scheduler) (`kernel/scheduler/tiku_sched.h`)
- [Virtual Filesystem (VFS)](#virtual-filesystem-vfs) (`server/vfs/tiku_vfs.h`)
- [Memory Management](#memory-management) (`kernel/memory/tiku_mem.h`)
- [Watchdog Timer](#watchdog-timer) (`kernel/cpu/tiku_watchdog.h`)
- [Common Utilities](#common-utilities) (`kernel/cpu/tiku_common.h`)

---

## Process Management

**Header:** `kernel/process/tiku_process.h`

### Types

```c
typedef struct tiku_process {
    struct tiku_process *next;      /* Next process in linked list          */
    const char *name;               /* Human-readable process name          */
    PT_THREAD((*thread)(...));      /* Process thread function              */
    struct pt pt;                   /* Protothread control state            */
    uint8_t is_running;             /* Non-zero if process is active        */
    void *local;                    /* Per-process local storage (or NULL)  */
} tiku_process_t;

typedef struct tiku_channel {
    uint8_t *buf;                   /* Pointer to message storage           */
    uint8_t  msg_size;              /* Size of each message in bytes        */
    uint8_t  capacity;              /* Maximum number of messages           */
    volatile uint8_t head;          /* Index of oldest message              */
    volatile uint8_t count;         /* Number of messages in channel        */
} tiku_channel_t;
```

### System Events

| Constant                  | Value  | Description                        |
|---------------------------|--------|------------------------------------|
| `TIKU_EVENT_INIT`         | `0x01` | Sent when a process is started     |
| `TIKU_EVENT_EXIT`         | `0x02` | Sent when a process is exiting     |
| `TIKU_EVENT_CONTINUE`     | `0x03` | Continue processing                |
| `TIKU_EVENT_POLL`         | `0x04` | Process poll request               |
| `TIKU_EVENT_EXITED`       | `0x05` | Broadcast when a process has exited|
| `TIKU_EVENT_FORCE_EXIT`   | `0x06` | Force-exit a process               |
| `TIKU_EVENT_USER`         | `0x10` | Base for user-defined events       |
| `TIKU_EVENT_TIMER`        | `0x88` | Timer expiration event             |

### Process Declaration Macros

#### TIKU_PROCESS(name, strname)

Declare and define a process with no local storage.

```c
TIKU_PROCESS(blink_process, "Blink");
```

#### TIKU_PROCESS_WITH_LOCAL(name, strname, local_type)

Declare a process with typed local storage. A static instance of `local_type`
is allocated and wired into the process struct at compile time.

```c
struct my_state { uint16_t counter; uint8_t flag; };
TIKU_PROCESS_WITH_LOCAL(my_proc, "my process", struct my_state);
```

#### TIKU_PROCESS_TYPED(name, strname, local_type)

Like `TIKU_PROCESS_WITH_LOCAL` but also generates a type-safe accessor
function `name_local()`.

```c
struct sensor_state { uint16_t reading; uint8_t count; };
TIKU_PROCESS_TYPED(sensor_proc, "sensor", struct sensor_state);

TIKU_PROCESS_THREAD(sensor_proc, ev, data) {
    struct sensor_state *s = sensor_proc_local();  /* fully type-safe */
    ...
}
```

#### TIKU_LOCAL(type)

Access the current process's local storage with a cast. Place above
`TIKU_PROCESS_BEGIN()`.

```c
TIKU_PROCESS_THREAD(my_proc, ev, data) {
    struct my_state *s = TIKU_LOCAL(struct my_state);
    TIKU_PROCESS_BEGIN();
    ...
}
```

### Thread Control Macros

| Macro | Description |
|-------|-------------|
| `TIKU_PROCESS_THREAD(name, ev, data)` | Declare a process thread function |
| `TIKU_PROCESS_BEGIN()` | Start the protothread block |
| `TIKU_PROCESS_END()` | End the protothread block |
| `TIKU_PROCESS_YIELD()` | Yield to other processes |
| `TIKU_PROCESS_YIELD_UNTIL(cond)` | Yield until condition is true |
| `TIKU_PROCESS_WAIT_EVENT()` | Wait for any event |
| `TIKU_PROCESS_WAIT_EVENT_UNTIL(cond)` | Wait for a specific event condition |
| `TIKU_PROCESS_EXIT()` | Exit the current process |
| `TIKU_PROCESS_CURRENT()` | Pointer to the currently running process |
| `TIKU_THIS()` | Alias for `TIKU_PROCESS_CURRENT()` |

#### TIKU_AUTOSTART_PROCESSES(...)

Register processes for automatic startup at boot.

```c
TIKU_AUTOSTART_PROCESSES(&proc_a, &proc_b);
```

### Channel Macros

#### TIKU_CHANNEL_DECLARE(name, type, depth)

Declare a typed, fixed-size message queue with generated inline accessors:
`name_init()`, `name_put(&msg)`, `name_get(&msg)`.

```c
TIKU_CHANNEL_DECLARE(sensor_ch, struct sensor_msg, 4);

sensor_ch_init();
sensor_ch_put(&msg);
sensor_ch_get(&msg);
```

### Functions

#### void tiku_process_init(void)

Initialize the process scheduler. Resets the process list and event queue.
Must be called once at system startup before any processes are started.

#### void tiku_process_start(struct tiku_process \*p, tiku_event_data_t data)

Start a process. Adds `p` to the active list and posts an `TIKU_EVENT_INIT`
event. Does nothing if the process is already running.

#### void tiku_process_exit(struct tiku_process \*p)

Exit a process. Marks it as stopped and removes it from the active list.

#### uint8_t tiku_process_post(struct tiku_process \*p, tiku_event_t ev, tiku_event_data_t data)

Post an event to a process. Use `TIKU_PROCESS_BROADCAST` as target to deliver
to all running processes. Returns 1 if posted, 0 if queue full.

#### uint8_t tiku_process_run(void)

Dequeue one event and dispatch it. Returns 0 when the event queue is empty
(safe to enter low-power mode).

#### void tiku_process_poll(struct tiku_process \*p)

Request a process to be polled. It will receive `TIKU_EVENT_POLL` on the next
scheduler run.

#### uint8_t tiku_process_is_running(struct tiku_process \*p)

Check if a process is running.

#### uint8_t tiku_process_queue_space(void) / queue_full(void) / queue_empty(void) / queue_length(void)

Event queue introspection helpers.

#### void tiku_autostart_start(struct tiku_process \*const processes[])

Start all processes in a NULL-terminated array.

### Channel Functions

#### void tiku_channel_init(struct tiku_channel \*ch, void \*buf, uint8_t msg_size, uint8_t capacity)

Initialize a channel with caller-provided storage.

#### uint8_t tiku_channel_put(struct tiku_channel \*ch, const void \*msg)

Put a message into a channel. Returns 1 if stored, 0 if full.

#### uint8_t tiku_channel_get(struct tiku_channel \*ch, void \*out)

Get a message from a channel. Returns 1 if retrieved, 0 if empty.

#### uint8_t tiku_channel_is_empty(struct tiku_channel \*ch)

Returns 1 if channel is empty.

#### uint8_t tiku_channel_free(struct tiku_channel \*ch)

Returns the number of free message slots.

---

## Protothreads

**Header:** `kernel/process/tiku_proto.h`

Stackless lightweight threads using only 2 bytes per thread. Built on top of
local continuations.

### Types

```c
struct pt {
    lc_t lc;    /* Local continuation state */
};
```

### Return Codes

| Constant      | Value | Description                     |
|---------------|-------|---------------------------------|
| `PT_WAITING`  | 0     | Thread is waiting for condition |
| `PT_YIELDED`  | 1     | Thread has yielded              |
| `PT_EXITED`   | 2     | Thread has exited               |
| `PT_ENDED`    | 3     | Thread has ended normally       |

### Macros

#### PT_INIT(pt)

Initialize a protothread control structure. Must be called before first use.

#### PT_THREAD(name_args)

Declare a protothread function. All protothread functions must use this macro.

```c
PT_THREAD(my_thread(struct pt *pt, int data))
{
    PT_BEGIN(pt);
    /* thread code */
    PT_END(pt);
}
```

#### PT_BEGIN(pt) / PT_END(pt)

Mark the beginning and end of a protothread. `PT_BEGIN` must be the first
statement; `PT_END` must be the last.

#### PT_WAIT_UNTIL(pt, condition)

Block until `condition` becomes true. Checked each time the thread is
scheduled.

#### PT_WAIT_WHILE(pt, cond)

Block while `cond` is true (inverse of `PT_WAIT_UNTIL`).

#### PT_WAIT_THREAD(pt, thread)

Wait for a child protothread to complete.

#### PT_SPAWN(pt, child, thread)

Initialize and wait for a child protothread. Combines `PT_INIT` +
`PT_WAIT_THREAD`.

```c
struct pt child_pt;
PT_SPAWN(pt, &child_pt, child_thread(&child_pt));
```

#### PT_RESTART(pt)

Restart the protothread from the beginning.

#### PT_EXIT(pt)

Exit the protothread immediately. Returns `PT_EXITED`.

#### PT_SCHEDULE(f)

Check if a protothread is still running. Returns non-zero for `PT_WAITING` and
`PT_YIELDED`, zero for `PT_EXITED` and `PT_ENDED`.

```c
while (PT_SCHEDULE(my_thread(&pt))) {
    /* thread is still running */
}
```

#### PT_YIELD(pt)

Voluntarily yield execution to other threads.

#### PT_YIELD_UNTIL(pt, cond)

Yield until a condition is met.

---

## Local Continuations

**Header:** `kernel/process/tiku_lc.h`

Low-level mechanism that captures and restores a function's execution state.
Foundation for protothreads.

### Types

```c
typedef unsigned short lc_t;
```

### Macros

| Macro | Description |
|-------|-------------|
| `LC_INIT(s)` | Initialize a local continuation variable |
| `LC_RESUME(s)` | Resume from a saved continuation point |
| `LC_SET(s)` | Save the current execution position |
| `LC_END(s)` | End the local continuation block |
| `LC_RESET(s)` | Reset continuation (alias for `LC_INIT`) |
| `LC_IS_RESUMED(s)` | Non-zero if continuation has been set |

> **Note:** `LC_SET` cannot be used inside another switch statement. Each
> `LC_SET` in a function must be on a different line.

---

## Software Timers

**Header:** `kernel/timers/tiku_timer.h`

Unified timer supporting two modes: event-driven (posts `TIKU_EVENT_TIMER` to
a process) and callback (calls a function directly).

### Types

```c
typedef void (*tiku_timer_callback_t)(void *ptr);

struct tiku_timer {
    struct tiku_timer *next;        /* Linked list pointer (internal)       */
    tiku_clock_time_t start;        /* When the timer was set               */
    tiku_clock_time_t interval;     /* Duration in clock ticks              */
    uint8_t mode;                   /* TIKU_TIMER_MODE_EVENT or _CALLBACK   */
    uint8_t active;                 /* Non-zero if in the active list       */
    struct tiku_process *p;         /* Event target or callback context     */
    tiku_timer_callback_t func;     /* Callback function (CALLBACK mode)    */
    void *ptr;                      /* User data for callback               */
};
```

### Constants

| Constant             | Description               |
|----------------------|---------------------------|
| `TIKU_TIMER_SECOND`  | One second in timer ticks |
| `TIKU_TIMER_MINUTE`  | One minute in timer ticks |

### Functions

#### void tiku_timer_init(void)

Initialize the timer subsystem. Call once during system init, after the
process system is up.

#### void tiku_timer_set_callback(struct tiku_timer \*t, tiku_clock_time_t ticks, tiku_timer_callback_t func, void \*ptr)

Set a callback timer. If already active, it is stopped and re-set. Callback
runs in the context of the calling process.

```c
static struct tiku_timer my_timer;
tiku_timer_set_callback(&my_timer, TIKU_CLOCK_SECOND * 2, on_timeout, NULL);
```

#### void tiku_timer_set_event(struct tiku_timer \*t, tiku_clock_time_t ticks)

Set an event timer. Posts `TIKU_EVENT_TIMER` to the calling process on expiry.

```c
static struct tiku_timer my_timer;
tiku_timer_set_event(&my_timer, TIKU_CLOCK_SECOND);
TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
```

#### void tiku_timer_reset(struct tiku_timer \*t)

Reset for drift-free periodic operation. Re-adds with
`start = old_start + interval`.

#### void tiku_timer_restart(struct tiku_timer \*t)

Restart anchored to current time. Use when drift doesn't matter (e.g.,
retriggering a timeout on activity).

#### void tiku_timer_stop(struct tiku_timer \*t)

Stop a timer. Safe to call even if not active.

#### int tiku_timer_expired(struct tiku_timer \*t)

Non-zero if expired (not in active list).

#### tiku_clock_time_t tiku_timer_remaining(struct tiku_timer \*t)

Ticks remaining until expiration. Returns 0 if expired.

#### tiku_clock_time_t tiku_timer_expiration_time(struct tiku_timer \*t)

Absolute tick at which this timer fires (`start + interval`).

#### int tiku_timer_any_pending(void)

Non-zero if at least one timer is active.

#### tiku_clock_time_t tiku_timer_next_expiration(void)

Nearest expiration time across all timers. Returns 0 if none pending. Useful
for the scheduler to know how long it can sleep.

#### void tiku_timer_request_poll(void)

Request the timer process to poll (called from clock ISR).

---

## System Clock

**Header:** `kernel/timers/tiku_clock.h`

### Types

```c
typedef unsigned short tiku_clock_time_t;
```

### Constants and Macros

| Macro | Description |
|-------|-------------|
| `TIKU_CLOCK_SECOND` | Number of clock ticks per second |
| `TIKU_CLOCK_LT(a, b)` | Wraparound-safe less-than comparison |
| `TIKU_CLOCK_DIFF(a, b)` | Wraparound-safe difference (`a - b`) |
| `TIKU_CLOCK_MS_TO_TICKS(ms)` | Convert milliseconds to clock ticks |

### Functions

#### void tiku_clock_init(void)

Initialize the system clock. Call once during system boot.

#### tiku_clock_time_t tiku_clock_time(void)

Get current clock time in ticks.

#### unsigned long tiku_clock_seconds(void)

Get current time in seconds since system start.

#### void tiku_clock_wait(tiku_clock_time_t t)

Busy-wait for specified clock ticks.

#### void tiku_clock_delay_usec(unsigned int dt)

CPU delay in microsecond-scale units (platform-specific calibration).

---

## Hardware Timer

**Header:** `kernel/timers/tiku_htimer.h`

Single-shot hardware timer with ISR-context callbacks. Only one htimer can be
pending at a time.

### Types

```c
typedef unsigned short tiku_htimer_clock_t;
typedef void (*tiku_htimer_callback_t)(struct tiku_htimer *t, void *ptr);

struct tiku_htimer {
    tiku_htimer_clock_t time;       /* Scheduled firing time (absolute)     */
    tiku_htimer_callback_t func;    /* ISR callback                         */
    void *ptr;                      /* User data for callback               */
};
```

### Return Codes

| Constant                   | Value | Description                    |
|----------------------------|-------|--------------------------------|
| `TIKU_HTIMER_OK`           |  0    | Success                        |
| `TIKU_HTIMER_ERR_TIME`     | -1    | Time too close or in the past  |
| `TIKU_HTIMER_ERR_INVALID`  | -2    | NULL timer or callback         |
| `TIKU_HTIMER_ERR_NONE`     | -3    | No timer to cancel             |

### Macros

| Macro | Description |
|-------|-------------|
| `TIKU_HTIMER_NOW()` | Read current hardware timer value |
| `TIKU_HTIMER_TIME(ht)` | Get the scheduled time of an htimer |
| `TIKU_HTIMER_SECOND` | Hardware timer ticks per second |
| `TIKU_HTIMER_GUARD_TIME` | Minimum ticks between now and scheduled time |
| `TIKU_HTIMER_CLOCK_DIFF(a, b)` | Signed difference with wraparound |
| `TIKU_HTIMER_CLOCK_LT(a, b)` | True if `a` is before `b` |

### Functions

#### void tiku_htimer_init(void)

Initialize the hardware timer subsystem. Call once during system init.

#### int tiku_htimer_set(struct tiku_htimer \*ht, tiku_htimer_clock_t time, tiku_htimer_callback_t func, void \*ptr)

Schedule a hardware timer. Only one can be active -- calling this overrides
any pending timer. Time must be at least `TIKU_HTIMER_GUARD_TIME` ticks in
the future.

Returns `TIKU_HTIMER_OK` on success.

#### int tiku_htimer_cancel(void)

Cancel the pending hardware timer. Returns `TIKU_HTIMER_OK` if cancelled,
`TIKU_HTIMER_ERR_NONE` if nothing was pending.

#### int tiku_htimer_is_scheduled(void)

Non-zero if a timer is currently scheduled.

#### void tiku_htimer_run_next(void)

Run the pending callback. **Only call from the hardware timer ISR.**

---

## Scheduler

**Header:** `kernel/scheduler/tiku_sched.h`

### Types

```c
typedef void (*tiku_sched_idle_hook_t)(void);
```

### Constants

| Constant              | Value | Description                    |
|-----------------------|-------|--------------------------------|
| `TIKU_SCHED_RUNNING`  | 0     | Scheduler is running normally  |
| `TIKU_SCHED_STOPPED`  | 1     | Scheduler has been asked to stop |

### Functions

#### void tiku_sched_init(void)

Initialize the scheduler and all managed subsystems (process system, software
timers, hardware timer). Call once at startup after clock init.

#### void tiku_sched_start(struct tiku_process \*p, tiku_event_data_t data)

Start a process through the scheduler. Convenience wrapper around
`tiku_process_start()`.

#### uint8_t tiku_sched_run_once(void)

Run one scheduler iteration. Checks for expired timers, then dispatches one
event. Returns 1 if work was done, 0 if idle.

#### void tiku_sched_loop(void)

Enter the main scheduler loop (**never returns**). Dispatches events, checks
timers, and calls the idle hook when no work is pending.

#### void tiku_sched_stop(void)

Stop the scheduler loop. Causes `tiku_sched_loop()` to return on its next
iteration. Primarily for test harnesses.

#### uint8_t tiku_sched_has_pending(void)

Non-zero if the event queue is non-empty or any timer is due.

#### void tiku_sched_set_idle_hook(tiku_sched_idle_hook_t hook)

Register an idle hook called when no work is pending. Pass `NULL` to clear.

#### void tiku_sched_notify(void)

Call from any ISR that generates work (e.g., clock tick). Requests the timer
process to poll for expired timers.

---

## Virtual Filesystem (VFS)

**Headers:** `server/vfs/tiku_vfs.h`, `server/vfs/tiku_vfs_tree.h`, `kernel/process/tiku_proc_vfs.h`

A static tree of named nodes that presents the entire system — peripherals,
OS state, processes, and configuration — as readable/writable paths. No block
storage, no inodes; just handler functions attached to path components.

### Types

```c
typedef enum {
    TIKU_VFS_DIR,
    TIKU_VFS_FILE
} tiku_vfs_type_t;

typedef int (*tiku_vfs_read_fn)(char *buf, size_t max);
typedef int (*tiku_vfs_write_fn)(const char *buf, size_t len);

typedef struct tiku_vfs_node {
    const char                  *name;        /* Path component             */
    tiku_vfs_type_t              type;        /* DIR or FILE                */
    tiku_vfs_read_fn             read;        /* NULL if not readable       */
    tiku_vfs_write_fn            write;       /* NULL if not writable       */
    const struct tiku_vfs_node  *children;    /* For DIR: child array       */
    uint8_t                      child_count; /* For DIR: number of children*/
} tiku_vfs_node_t;

typedef void (*tiku_vfs_list_fn)(const tiku_vfs_node_t *node, void *ctx);
```

### Core API Functions

#### void tiku_vfs_init(const tiku_vfs_node_t \*root)

Register the root node. All subsequent resolve/read/write calls walk from this
root.

#### const tiku_vfs_node_t \*tiku_vfs_resolve(const char \*path)

Walk the tree to find a node by path. Returns `NULL` if not found. Handles
leading/trailing/duplicate slashes.

```c
const tiku_vfs_node_t *n = tiku_vfs_resolve("/sys/mem/free");
if (n && n->type == TIKU_VFS_FILE && n->read) {
    n->read(buf, sizeof(buf));
}
```

#### int tiku_vfs_read(const char \*path, char \*buf, size_t max)

Resolve path and invoke the file's read handler. Returns bytes written to buf,
or -1 on error (not found, not a file, no read handler).

#### int tiku_vfs_write(const char \*path, const char \*data, size_t len)

Resolve path and invoke the file's write handler. Returns 0 on success, -1 on
error.

#### int tiku_vfs_list(const char \*path, tiku_vfs_list_fn callback, void \*ctx)

List children of a directory node. Calls `callback` once per child with the
full node pointer and user context. Returns -1 if not a directory.

```c
static void print_entry(const tiku_vfs_node_t *node, void *ctx) {
    const char *perm = (node->read && node->write) ? "rw"
                     : node->read                  ? "r-" : "--";
    printf("  %s %s%s\n", perm, node->name,
           node->type == TIKU_VFS_DIR ? "/" : "");
}

tiku_vfs_list("/dev", print_entry, NULL);
```

### Tree Initialization

#### void tiku_vfs_tree_init(void)

Build and register the production VFS tree with `/sys`, `/dev`, and `/proc`.
Call once during boot, after processes are registered (so `/proc/` captures
them). Internally unlocks the MPU to write FRAM-backed node arrays.

#### void tiku_vfs_set_boot_count(uint32_t count)

Set the boot count value exposed via `/sys/boot/count`. Call from the hibernate
resume path after reading the marker.

### Process VFS

#### const tiku_vfs_node_t \*tiku_proc_vfs_get(void)

Build and return the `/proc` directory node. Rebuilds per-pid directories from
the process registry each time it is called. The returned node's children
pointer remains valid until the next call.

#### uint8_t tiku_proc_vfs_child_count(void)

Return the number of children under `/proc` (count + queue + catalog + one per
registered process).

### Complete Node Tree

All paths are accessible via the shell (`cat`, `ls`, `write`) and programmatic
API (`tiku_vfs_read`, `tiku_vfs_write`).

```
/
├── sys/
│   ├── version              r-   OS version string
│   ├── device               r-   MCU name
│   ├── uptime               r-   seconds since boot
│   ├── mem/
│   │   ├── sram             r-   RAM size in bytes
│   │   ├── nvm              r-   FRAM size in bytes
│   │   ├── free             r-   live stack headroom (SP - BSS end)
│   │   └── used             r-   sum of per-process SRAM allocation
│   ├── cpu/
│   │   └── freq             r-   clock frequency in Hz
│   ├── power/
│   │   ├── mode             r-   current LPM (off/LPM0/LPM3/LPM4)
│   │   └── wake             r-   active wake sources
│   ├── timer/
│   │   ├── count            r-   active software timers
│   │   ├── next             r-   ticks until next expiration
│   │   ├── fired            r-   total expirations since boot
│   │   └── list/
│   │       ├── 0            r-   timer 0: mode, remaining, interval
│   │       ├── 1            r-   timer 1
│   │       ├── 2            r-   timer 2
│   │       └── 3            r-   timer 3
│   ├── clock/
│   │   └── ticks            r-   raw tick counter
│   ├── watchdog/
│   │   └── mode             r-   "watchdog" or "interval"
│   ├── htimer/
│   │   ├── now              r-   hardware timer counter
│   │   └── scheduled        r-   1 if pending, 0 if idle
│   ├── boot/
│   │   ├── reason           r-   last reset cause (brownout/wdt/rstnmi/...)
│   │   └── count            r-   hibernate boot counter
│   └── sched/
│       └── idle             r-   scheduler idle entry count
├── dev/
│   ├── led0                 rw   LED1 state (0, 1, t=toggle)
│   ├── led1                 rw   LED2 state
│   ├── gpio/
│   │   └── {1..4}/          per-port directory
│   │       └── {0..7}       rw   pin state (0, 1, t=toggle, i=input)
│   ├── gpio_dir/
│   │   └── {1..4}           r-   pin directions (I=input, O=output)
│   ├── uart/
│   │   ├── overruns         r-   UART overrun count since boot
│   │   └── baud             r-   configured baud rate
│   ├── adc/
│   │   ├── temp             r-   on-chip temperature sensor (raw ADC)
│   │   └── battery          r-   battery voltage (raw ADC)
│   ├── i2c/
│   │   └── scan             r-   list responding I2C slave addresses
│   └── spi/
│       └── config           r-   SPI mode, bit order, prescaler (or "n/a")
└── proc/
    ├── count                r-   number of active processes
    ├── queue/
    │   ├── length           r-   pending events in queue
    │   └── space            r-   free event slots
    ├── catalog/
    │   ├── count            r-   available-but-not-started processes
    │   └── {0,1}/
    │       └── name         r-   catalog entry name
    └── {0..7}/              per-process directory (one per registered process)
        ├── name             r-   process name
        ├── state            r-   running/ready/waiting/sleeping/stopped
        ├── pid              r-   numeric process id
        ├── sram_used        r-   SRAM bytes allocated
        ├── fram_used        r-   FRAM bytes allocated
        ├── uptime           r-   seconds since start
        ├── wake_count       r-   times scheduled
        └── events           r-   pending events for this process
```

### Adding a New VFS Node

1. Write a read handler (and optionally a write handler):
   ```c
   static int my_read(char *buf, size_t max) {
       return snprintf(buf, max, "%u\n", my_value);
   }
   ```

2. Add a `tiku_vfs_node_t` entry to the appropriate children array in
   `server/vfs/tiku_vfs_tree.c`:
   ```c
   { "my_node", TIKU_VFS_FILE, my_read, NULL, NULL, 0 },
   ```

3. Update the parent directory's `child_count`.

4. If the node is in FRAM (`.persistent` section), wrap writes in
   `tiku_mpu_unlock_nvm()` / `tiku_mpu_lock_nvm()`.

### Shell Commands

| Command | VFS Operation | Example |
|---------|---------------|---------|
| `ls [path]` | `tiku_vfs_list()` | `ls /sys/mem` — lists `sram nvm free used` with `r-`/`rw`/`d` |
| `cat <path>` | `tiku_vfs_read()` | `cat /sys/uptime` — prints seconds since boot |
| `read <path>` | `tiku_vfs_read()` | Alias for `cat` |
| `write <path> <value>` | `tiku_vfs_write()` | `write /dev/led0 1` — turn on LED |
| `echo <path> <value>` | `tiku_vfs_write()` | Alias for `write` |
| `toggle <path>` | read, flip, write | `toggle /dev/led0` — toggle a binary node |
| `cd <path>` | set working directory | `cd /sys/mem` then `cat free` |

### Testing

**On-device tests** (`tests/server/vfs/test_vfs.c`, `test_vfs_tree.c`):

```bash
# Stub-based core VFS tests (path resolution, read/write, list, edge cases)
python3 TikuBench/tikubench/runner.py --category vfs

# Live production tree tests (all /sys, /dev, /proc nodes on real hardware)
python3 TikuBench/tikubench/runner.py --category vfs-tree
```

**Python serial test** (`TikuBench/tikubench/vfs_test.py`):

Exhaustive over-the-wire test that sends shell commands and validates responses
for every VFS path.

```bash
python3 TikuBench/tikubench/vfs_test.py --port /dev/ttyUSB0 --baud 115200 --verbose
```

---

## Memory Management

**Header:** `kernel/memory/tiku_mem.h`

Static memory management with arenas (bump-pointer), pools (fixed-block),
persistent NVM key-value store, MPU protection, tier allocation, write-back
caching, and hibernate/resume support.

### Error Codes

```c
typedef enum {
    TIKU_MEM_OK            =  0,    /* Success                              */
    TIKU_MEM_ERR_INVALID   = -1,    /* Invalid argument                     */
    TIKU_MEM_ERR_NOMEM     = -2,    /* Out of memory                        */
    TIKU_MEM_ERR_FULL      = -3,    /* Store is full                        */
    TIKU_MEM_ERR_NOT_FOUND = -4     /* Key not found                        */
} tiku_mem_err_t;
```

### Memory Tiers

```c
typedef enum {
    TIKU_MEM_SRAM = 0,  /* Fast, volatile                                   */
    TIKU_MEM_NVM  = 1,  /* Persistent, slower writes (FRAM)                 */
    TIKU_MEM_AUTO = 2   /* OS selects: prefers SRAM, falls back to NVM      */
} tiku_mem_tier_t;
```

### Arena Allocator

Bump-pointer allocator. No individual free -- reset discards everything at
once.

```c
typedef struct {
    uint8_t              *buf;
    tiku_mem_arch_size_t  capacity;
    tiku_mem_arch_size_t  offset;
    tiku_mem_arch_size_t  peak;
    tiku_mem_arch_size_t  count;
    uint8_t               id;
    uint8_t               active;
    tiku_mem_tier_t       tier;
} tiku_arena_t;
```

| Function | Description |
|----------|-------------|
| `tiku_arena_create(arena, buf, size, id)` | Initialize over a caller-provided buffer with region validation |
| `tiku_arena_create_raw(arena, buf, size)` | Initialize without region-registry checks (for library code) |
| `tiku_arena_alloc(arena, size)` | Allocate aligned memory. Returns `NULL` if full |
| `tiku_arena_reset(arena)` | Reset offset to zero. O(1), preserves peak |
| `tiku_arena_secure_reset(arena)` | Zero all memory, then reset. O(n) |
| `tiku_arena_stats(arena, stats)` | Snapshot of usage statistics |

### Pool Allocator

Fixed-size block allocator with embedded freelist. O(1) alloc and free, zero
per-block metadata overhead.

```c
typedef struct {
    uint8_t              *buf;
    tiku_mem_arch_size_t  block_size;
    tiku_mem_arch_size_t  block_count;
    void                 *free_head;
    tiku_mem_arch_size_t  used_count;
    tiku_mem_arch_size_t  peak_count;
    uint8_t               id;
    uint8_t               active;
    tiku_mem_tier_t       tier;
} tiku_pool_t;
```

| Function | Description |
|----------|-------------|
| `tiku_pool_create(pool, buf, block_size, block_count, id)` | Initialize with region validation |
| `tiku_pool_create_raw(pool, buf, block_size, block_count)` | Initialize without region checks |
| `tiku_pool_alloc(pool)` | Pop a block from the freelist. Returns `NULL` if empty |
| `tiku_pool_free(pool, ptr)` | Push a block back. Validates alignment and range |
| `tiku_pool_reset(pool)` | Return all blocks to freelist. O(n) |
| `tiku_pool_stats(pool, stats)` | Snapshot of usage statistics |

### Region Registry

Platform memory map management and overlap detection.

| Function | Description |
|----------|-------------|
| `tiku_region_init(table, count)` | Register platform memory regions |
| `tiku_region_contains(ptr, size, type)` | Check if range falls within a region of expected type |
| `tiku_region_claim(ptr, size, owner_id)` | Claim a memory range for a subsystem |
| `tiku_region_unclaim(ptr)` | Release a previously claimed range |
| `tiku_region_get_type(ptr, out_type)` | Look up the region type for an address |

### Persistent NVM Key-Value Store

```c
typedef struct { ... } tiku_persist_store_t;
```

| Function | Description |
|----------|-------------|
| `tiku_persist_init(store)` | Initialize store, recover valid entries from NVM |
| `tiku_persist_register(store, key, fram_buf, capacity)` | Register an NVM buffer under a key |
| `tiku_persist_read(store, key, buf, buf_size, out_len)` | Read value into SRAM buffer |
| `tiku_persist_write(store, key, data, data_len)` | Write from SRAM into NVM |
| `tiku_persist_delete(store, key)` | Delete an entry |
| `tiku_persist_wear_check(store, key, write_count)` | Check write endurance level |

### MPU (Memory Protection Unit)

```c
typedef enum { TIKU_MPU_SEG1, TIKU_MPU_SEG2, TIKU_MPU_SEG3 } tiku_mpu_seg_t;
typedef enum { TIKU_MPU_READ=0x01, TIKU_MPU_WRITE=0x02, TIKU_MPU_EXEC=0x04,
               TIKU_MPU_RD_WR=0x03, TIKU_MPU_RD_EXEC=0x05, TIKU_MPU_ALL=0x07
             } tiku_mpu_perm_t;
```

| Function | Description |
|----------|-------------|
| `tiku_mpu_init()` | Initialize MPU with default NVM protection |
| `tiku_mpu_set_permissions(seg, perm)` | Set permissions on one segment |
| `tiku_mpu_unlock_nvm()` | Unlock NVM for writing; returns saved state |
| `tiku_mpu_lock_nvm(saved)` | Restore MPU state after NVM write |
| `tiku_mpu_scoped_write(fn, ctx)` | Execute `fn` with NVM unlocked, interrupts disabled |
| `tiku_mpu_enable_violation_nmi()` | Switch MPU violation response from reset to NMI |
| `tiku_mpu_get_violation_flags()` | Read per-segment violation flags |
| `tiku_mpu_clear_violation_flags()` | Clear all violation flags |

### Tier Allocator

Creates arenas and pools backed by a specific memory tier (SRAM or NVM).

| Function | Description |
|----------|-------------|
| `tiku_tier_init()` | Initialize tier allocator (after `tiku_mem_init()`) |
| `tiku_tier_arena_create(arena, tier, size, id)` | Create a tier-backed arena |
| `tiku_tier_pool_create(pool, tier, block_size, block_count, id)` | Create a tier-backed pool |
| `tiku_tier_get(ptr, out_tier)` | Query which tier a pointer belongs to |
| `tiku_tier_stats(tier, stats)` | Usage statistics for a tier's backing pool |

### Write-Back Cache (SRAM/FRAM)

Pairs an SRAM working copy with a FRAM persistent backing store.

```c
typedef struct {
    uint8_t              *sram_cache;
    uint8_t              *fram_backing;
    tiku_mem_arch_size_t  size;
    uint8_t               dirty;
    uint8_t               active;
} tiku_cached_region_t;
```

| Function | Description |
|----------|-------------|
| `tiku_cache_create(region, fram_addr, sram_buf, size)` | Create a cached region |
| `tiku_cache_get(region)` | Get SRAM pointer (marks dirty) |
| `tiku_cache_mark_dirty(region)` | Explicitly mark as dirty |
| `tiku_cache_flush(region)` | Flush one region to FRAM |
| `tiku_cache_flush_all()` | Flush all dirty regions (call before LPM) |
| `tiku_cache_reload(region)` | Reload SRAM from FRAM |
| `tiku_cache_destroy(region)` | Destroy and unregister a cached region |
| `tiku_cache_get_count()` | Number of registered cached regions |
| `tiku_cache_get_region(index)` | Get cached region by index |

### Process Memory Context

Binds SRAM scratch + NVM persistent arenas + cached regions to a process.

| Function | Description |
|----------|-------------|
| `tiku_proc_mem_create(pmem, pid, tier, sram_size, nvm_size)` | Create isolated memory context |
| `tiku_proc_mem_destroy(pmem)` | Flush caches, reset arenas, deactivate |
| `tiku_proc_alloc(pmem, tier, size)` | Allocate within a process context |
| `tiku_proc_mem_attach_cache(pmem, region)` | Attach a cached region to the context |
| `tiku_proc_mem_stats(pmem, tier, stats)` | Stats for a process arena |

### Hibernate / Resume

| Function | Description |
|----------|-------------|
| `tiku_mem_hibernate(fram_buf, timestamp)` | Flush caches, write hibernate marker to FRAM |
| `tiku_mem_resume(fram_buf, marker_out)` | Check for warm resume; reload caches if valid |
| `tiku_mem_hibernate_reset()` | Reset hibernate state (for test harnesses) |

#### void tiku_mem_init(void)

Entry point for the memory subsystem. Initializes region registry, activates
MPU protection, and performs platform-specific memory setup.

---

## Watchdog Timer

**Header:** `kernel/cpu/tiku_watchdog.h`

| Function | Description |
|----------|-------------|
| `tiku_watchdog_init()` | Initialize with default settings |
| `tiku_watchdog_config(mode, clk, interval, start_held, kick_on_start)` | Configure with custom parameters |
| `tiku_watchdog_kick()` | Kick (reset) to prevent timeout |
| `tiku_watchdog_pause()` | Pause the watchdog |
| `tiku_watchdog_resume()` | Resume the watchdog |
| `tiku_watchdog_resume_with_kick()` | Resume with an immediate kick |
| `tiku_watchdog_off()` | Disable entirely |

---

## Common Utilities

**Header:** `kernel/cpu/tiku_common.h`

### Functions

| Function | Description |
|----------|-------------|
| `tiku_common_delay_ms(ms)` | Blocking delay in milliseconds |
| `tiku_common_led1_init()` | Initialize LED1 hardware |
| `tiku_common_led2_init()` | Initialize LED2 hardware |
| `tiku_common_led1_on()` / `led1_off()` / `led1_toggle()` | LED1 control |
| `tiku_common_led2_on()` / `led2_off()` / `led2_toggle()` | LED2 control |
