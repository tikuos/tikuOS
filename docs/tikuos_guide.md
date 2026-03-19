# TikuOS: Comprehensive Guide

## What is TikuOS?

TikuOS is an embedded operating system for ultra-low-power MSP430
microcontrollers. It provides cooperative multitasking through
protothreads, event-driven scheduling, software and hardware timers,
memory management with MPU protection, and a rich library ecosystem
(TikuKits) covering data structures, ML inference, signal processing,
compression, and IPv4 networking.

**Target hardware:** TI MSP430FR5969 (2 KB RAM, 64 KB FRAM, 8 MHz)

**Design principles:**
- Zero dynamic allocation -- all memory is statically sized
- Cooperative scheduling -- no preemption, no per-thread stacks
- Event-driven -- processes communicate via a 32-entry event queue
- Low power -- idle hook enters LPM3 when no work is pending
- Minimal footprint -- kernel fits in ~3 KB of code

---

## Part 1: Getting Started

### 1.1 Your First Application: LED Blink

Every TikuOS application is a **process** -- a function that runs as a
lightweight protothread. Here is the simplest possible application:

```c
#include "tiku.h"

/* 1. Declare the process */
TIKU_PROCESS(blink_process, "Blink");

/* 2. Register it for automatic startup */
TIKU_AUTOSTART_PROCESSES(&blink_process);

/* 3. Define the process thread */
TIKU_PROCESS_THREAD(blink_process, ev, data)
{
    static struct tiku_timer tmr;

    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_timer_set_event(&tmr, TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_common_led1_toggle();
        tiku_timer_reset(&tmr);    /* drift-free reschedule */
    }

    TIKU_PROCESS_END();
}
```

**What happens at boot:**
1. `main()` calls `tiku_sched_init()` (initializes process system,
   timers, hardware timer)
2. `tiku_sched_loop()` starts autostart processes and enables interrupts
3. The blink process receives `TIKU_EVENT_INIT`, sets a 1-second timer
4. Every second, the timer fires, the process toggles the LED

**Build and flash:**
```bash
make MCU=msp430fr5969
make flash MCU=msp430fr5969
```

### 1.2 Key Concepts

| Concept | What it is |
|---------|------------|
| **Process** | A function with preserved execution state (protothread) |
| **Event** | A (type, data) pair delivered to a process via a queue |
| **Timer** | Fires after N ticks, either posting an event or calling a callback |
| **Channel** | A typed FIFO message buffer between processes |
| **Scheduler** | Drains the event queue, checks timers, enters idle |

### 1.3 Building

```bash
# Build for MSP430FR5969
make MCU=msp430fr5969

# Build with CLI application
make APP=cli MCU=msp430fr5969

# Flash to hardware
make flash MCU=msp430fr5969

# Open serial monitor
make monitor

# Clean build artifacts
make clean
```

---

## Part 2: The Process Model

### 2.1 Declaring a Process

```c
/* Simple process (no local storage) */
TIKU_PROCESS(my_proc, "My Process");

/* Process with typed local storage */
TIKU_PROCESS_WITH_LOCAL(sensor_proc, "Sensor", struct sensor_state);

/* Process with type-safe accessor function */
TIKU_PROCESS_TYPED(logger_proc, "Logger", struct log_state);
/* generates: struct log_state *logger_proc_local(void) */
```

### 2.2 Writing a Process Thread

A process thread looks like a normal function but uses protothread
macros to yield and wait:

```c
TIKU_PROCESS_THREAD(my_proc, ev, data)
{
    TIKU_PROCESS_BEGIN();      /* MUST be first statement */

    /* One-time initialization */
    printf("Process started\n");

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();   /* Yield until any event */

        if (ev == MY_CUSTOM_EVENT) {
            /* Handle event */
        }
    }

    TIKU_PROCESS_END();        /* MUST be last statement */
}
```

**Important rules:**
- Local variables are NOT preserved across `WAIT_EVENT` or `YIELD`.
  Use `static` variables for state that must persist.
- No `switch` statements allowed (protothreads use switch internally).
- `PROCESS_BEGIN` / `PROCESS_END` must bracket the entire body.

### 2.3 Process Lifecycle

```
  TIKU_PROCESS_START()
         |
         v
    TIKU_EVENT_INIT delivered
         |
         v
    Process body runs until WAIT_EVENT
         |
    +----+----+
    |         |
    v         v
  event    PROCESS_END
  arrives  (auto-exit)
    |
    v
  body resumes from WAIT_EVENT
    |
    ... (repeat) ...
```

### 2.4 Starting Processes

```c
/* Automatic startup (in global scope) */
TIKU_AUTOSTART_PROCESSES(&proc_a, &proc_b);

/* Manual startup from another process */
tiku_process_start(&my_proc, NULL);

/* Manual startup via scheduler API */
tiku_sched_start(&my_proc, some_data);
```

### 2.5 Stopping Processes

```c
/* From within the process thread */
TIKU_PROCESS_EXIT();

/* From outside */
tiku_process_exit(&my_proc);

/* Let it end naturally */
TIKU_PROCESS_END();  /* returns PT_ENDED, auto-exits */
```

---

## Part 3: Events and Communication

### 3.1 System Events

| Event | Value | Meaning |
|-------|:-----:|---------|
| `TIKU_EVENT_INIT` | 0x01 | Process just started |
| `TIKU_EVENT_EXIT` | 0x02 | Graceful exit request |
| `TIKU_EVENT_CONTINUE` | 0x03 | Generic continuation |
| `TIKU_EVENT_POLL` | 0x04 | Polling wakeup |
| `TIKU_EVENT_TIMER` | 0x88 | Software timer expired |
| `TIKU_EVENT_USER` | 0x10 | Base for user-defined events |

### 3.2 Posting Events

```c
/* To a specific process */
tiku_process_post(&target_proc, MY_EVENT, my_data);

/* Broadcast to all processes */
tiku_process_post(TIKU_PROCESS_BROADCAST, MY_EVENT, NULL);

/* Poll a process (posts TIKU_EVENT_POLL) */
tiku_process_poll(&target_proc);
```

### 3.3 Waiting for Events

```c
/* Wait for any event */
TIKU_PROCESS_WAIT_EVENT();

/* Wait for a specific condition */
TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

/* Yield (return immediately, resume on next event) */
TIKU_PROCESS_YIELD();
```

### 3.4 Channels (Type-Safe Message Passing)

Channels are typed circular buffers for passing structured data
between processes:

```c
/* Declare a channel: name, message type, depth */
TIKU_CHANNEL_DECLARE(sensor_ch, struct sensor_msg, 4);

/* Initialize (call once) */
sensor_ch_init();

/* Producer: put a message */
struct sensor_msg msg = { .seq = 1, .value = 42 };
if (sensor_ch_put(&msg)) {
    tiku_process_post(&consumer, EVENT_DATA_READY, NULL);
}

/* Consumer: get messages */
struct sensor_msg rx;
while (sensor_ch_get(&rx)) {
    printf("Seq %u: value %u\n", rx.seq, rx.value);
}

/* Query state */
tiku_channel_is_empty(&sensor_ch);   /* 1 if empty */
tiku_channel_free(&sensor_ch);       /* free slots */
```

---

## Part 4: Timers

### 4.1 Event Timers

An event timer posts `TIKU_EVENT_TIMER` to the calling process when
it expires:

```c
static struct tiku_timer my_timer;

/* Set a one-second timer */
tiku_timer_set_event(&my_timer, TIKU_CLOCK_SECOND);

/* Wait for it */
TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

/* Reschedule (drift-free) */
tiku_timer_reset(&my_timer);

/* Or reschedule from now (may drift) */
tiku_timer_restart(&my_timer);
```

### 4.2 Callback Timers

A callback timer calls a function directly instead of posting an event:

```c
static struct tiku_timer cb_timer;

static void on_timeout(void *ptr)
{
    /* Runs in timer process context, NOT ISR */
    toggle_led();
    tiku_timer_reset(&cb_timer);  /* periodic */
}

/* Set it */
tiku_timer_set_callback(&cb_timer, TIKU_CLOCK_SECOND / 2,
                        on_timeout, NULL);
```

### 4.3 Timer Queries

```c
tiku_timer_expired(&tmr);          /* 1 if not active */
tiku_timer_remaining(&tmr);        /* ticks until fire */
tiku_timer_expiration_time(&tmr);  /* absolute fire time */
tiku_timer_any_pending();          /* any timer active? */
tiku_timer_stop(&tmr);             /* cancel timer */
```

### 4.4 Hardware Timers (Microsecond Precision)

For sub-millisecond timing, use hardware timers (ISR-level callbacks):

```c
static struct tiku_htimer ht;

static void on_hw_timeout(struct tiku_htimer *t, void *ptr)
{
    /* Runs in ISR context! Must be fast. */
    toggle_pin();

    /* Reschedule for drift-free periodic operation */
    tiku_htimer_set(t, TIKU_HTIMER_TIME(t) + PERIOD, on_hw_timeout, NULL);
}

/* Schedule 25 ms from now */
tiku_htimer_set(&ht, TIKU_HTIMER_NOW() + TIKU_HTIMER_SECOND / 40,
                on_hw_timeout, NULL);
```

---

## Part 5: The Scheduler

### 5.1 Startup Sequence

```c
int main(void)
{
    tiku_watchdog_off();
    tiku_cpu_full_init(8000000);   /* 8 MHz */

    tiku_sched_init();   /* Init process + timer subsystems */
    tiku_sched_loop();   /* Never returns */
}
```

`tiku_sched_loop()` does:
1. Starts autostart processes
2. Enables global interrupts
3. Enters the main loop:
   - Drain all pending events (`tiku_sched_run_once()`)
   - If idle: call idle hook (enter low-power mode)
   - ISR wakes CPU, loop repeats

### 5.2 Idle Hook (Low Power)

```c
static void enter_lpm(void)
{
    /* Platform enters LPM3 -- wakes on any interrupt */
    __bis_SR_register(LPM3_bits | GIE);
}

tiku_sched_set_idle_hook(enter_lpm);
```

### 5.3 Event Queue

The scheduler uses a 32-entry circular event queue:

```
[event] [event] [event] ... [empty slots]
  ^head                      ^tail
```

- `tiku_process_post()` enqueues (ISR-safe, atomic on MSP430)
- `tiku_process_run()` dequeues and dispatches one event
- Queue full: `post()` returns 0 (event dropped)
- Broadcasts deliver to all processes in the process list

---

## Part 6: Memory Management

### 6.1 Arena Allocator (Bump Pointer)

For temporary allocations that are freed all at once:

```c
static uint8_t buf[256];
static tiku_arena_t arena;

tiku_arena_create(&arena, buf, sizeof(buf), 1);

void *p1 = tiku_arena_alloc(&arena, 32);   /* 32 bytes */
void *p2 = tiku_arena_alloc(&arena, 16);   /* 16 bytes */

tiku_arena_reset(&arena);  /* Free everything (O(1)) */
```

### 6.2 Pool Allocator (Fixed-Size Blocks)

For repeated alloc/free of same-sized objects:

```c
static uint8_t buf[128];
static tiku_pool_t pool;

tiku_pool_create(&pool, buf, 16, 8, 1);  /* 8 blocks of 16 bytes */

void *blk = tiku_pool_alloc(&pool);
/* ... use blk ... */
tiku_pool_free(&pool, blk);
```

### 6.3 Persistent FRAM Store

Key-value store that survives reboots (MSP430 FRAM):

```c
static tiku_persist_store_t store;
static uint8_t __attribute__((persistent)) cal_buf[32];

tiku_persist_init(&store);
tiku_persist_register(&store, "cal", cal_buf, sizeof(cal_buf));

/* Write (requires MPU unlock for FRAM) */
uint16_t saved = tiku_mpu_unlock_nvm();
tiku_persist_write(&store, "cal", data, len);
tiku_mpu_lock_nvm(saved);

/* Read (no unlock needed) */
tiku_persist_read(&store, "cal", buf, sizeof(buf), &out_len);
```

### 6.4 Region Registry

Tracks platform memory regions (SRAM, NVM, peripheral):

```c
tiku_region_contains(ptr, size, TIKU_MEM_REGION_SRAM);  /* 1 if in SRAM */
tiku_region_claim(ptr, size, MY_SUBSYSTEM_ID);           /* claim range */
tiku_region_unclaim(ptr);                                 /* release */
```

### 6.5 MPU Write Protection

Protects FRAM from accidental writes:

```c
/* Quick write with automatic lock/unlock */
tiku_mpu_scoped_write(my_write_func, my_context);

/* Manual control */
uint16_t saved = tiku_mpu_unlock_nvm();
/* ... write to FRAM ... */
tiku_mpu_lock_nvm(saved);
```

---

## Part 7: Walkthrough Examples

### Example 1: Two Processes Communicating

A producer sends counter values to a consumer:

```c
#define EVENT_NEW_DATA  (TIKU_EVENT_USER + 0)

TIKU_PROCESS(producer, "Producer");
TIKU_PROCESS(consumer, "Consumer");

TIKU_AUTOSTART_PROCESSES(&producer, &consumer);

/* Producer: send a counter every 2 seconds */
TIKU_PROCESS_THREAD(producer, ev, data)
{
    static struct tiku_timer tmr;
    static int counter = 0;

    TIKU_PROCESS_BEGIN();

    tiku_timer_set_event(&tmr, 2 * TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_process_post(&consumer, EVENT_NEW_DATA,
                          (void *)(intptr_t)counter++);
        tiku_timer_reset(&tmr);
    }

    TIKU_PROCESS_END();
}

/* Consumer: receive and print */
TIKU_PROCESS_THREAD(consumer, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_NEW_DATA);
        int value = (int)(intptr_t)data;
        printf("Received: %d\n", value);
    }

    TIKU_PROCESS_END();
}
```

### Example 2: Callback Timer LED Pattern

Two independent LEDs blinking at different rates:

```c
static struct tiku_timer tmr_a, tmr_b;

static void blink_a(void *ptr) {
    tiku_common_led1_toggle();
    tiku_timer_reset(&tmr_a);
}

static void blink_b(void *ptr) {
    tiku_common_led2_toggle();
    tiku_timer_reset(&tmr_b);
}

TIKU_PROCESS(pattern, "Pattern");
TIKU_AUTOSTART_PROCESSES(&pattern);

TIKU_PROCESS_THREAD(pattern, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_timer_set_callback(&tmr_a, TIKU_CLOCK_SECOND / 2,
                            blink_a, NULL);
    tiku_timer_set_callback(&tmr_b, TIKU_CLOCK_SECOND * 13 / 10,
                            blink_b, NULL);

    /* Nothing to do -- timers handle everything */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT();
    }

    TIKU_PROCESS_END();
}
```

### Example 3: Channel-Based Sensor Pipeline

```c
struct reading { uint16_t seq; uint16_t value; };

TIKU_CHANNEL_DECLARE(readings, struct reading, 8);

#define EVENT_DATA_READY  (TIKU_EVENT_USER + 0)

TIKU_PROCESS(sampler, "Sampler");
TIKU_PROCESS(logger, "Logger");
TIKU_AUTOSTART_PROCESSES(&sampler, &logger);

/* Sampler: produce readings every 500ms */
TIKU_PROCESS_THREAD(sampler, ev, data)
{
    static struct tiku_timer tmr;
    static uint16_t seq = 0;

    TIKU_PROCESS_BEGIN();
    readings_init();
    tiku_timer_set_event(&tmr, TIKU_CLOCK_SECOND / 2);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        struct reading r = { .seq = seq++, .value = read_adc() };
        if (readings_put(&r)) {
            tiku_process_post(&logger, EVENT_DATA_READY, NULL);
        }
        tiku_timer_reset(&tmr);
    }

    TIKU_PROCESS_END();
}

/* Logger: drain channel on notification */
TIKU_PROCESS_THREAD(logger, ev, data)
{
    TIKU_PROCESS_BEGIN();

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == EVENT_DATA_READY);

        struct reading r;
        while (readings_get(&r)) {
            printf("#%u: %u\n", r.seq, r.value);
        }
    }

    TIKU_PROCESS_END();
}
```

---

## Part 8: Kernel Internals

### 8.1 Protothread Mechanics

TikuOS protothreads use Duff's device (switch/case on `__LINE__`):

```c
/* PT_BEGIN expands to: */
{
    char PT_YIELD_FLAG = 1;
    switch(pt->lc) { case 0:

/* PT_YIELD expands to: */
    PT_YIELD_FLAG = 0;
    pt->lc = __LINE__; case __LINE__:;
    if (PT_YIELD_FLAG == 0) return PT_YIELDED;

/* PT_END expands to: */
    } pt->lc = 0; return PT_ENDED; }
```

**How it works:**
1. First call: `pt->lc` is 0, `switch` falls through from `case 0:`
2. Code runs until `PT_YIELD`: saves `__LINE__`, returns `PT_YIELDED`
3. Next call: `switch` jumps to the saved `case __LINE__:` label
4. `PT_YIELD_FLAG` is 1 (set by `PT_BEGIN`), so the yield check
   is false -- execution continues past the yield point
5. Repeat until `PT_END` or `PT_EXIT`

**Limitations:**
- Local variables are on the C stack -- lost on yield
- No `switch` statements inside the thread (conflicts with outer switch)
- Each `PT_YIELD` must be on a unique line number

### 8.2 Event Queue Implementation

```c
struct event_item {
    tiku_event_data_t data;    /* void * */
    struct tiku_process *p;    /* target (NULL = broadcast) */
    tiku_event_t ev;           /* event type (uint8_t) */
};

static struct event_item queue[32];  /* power of 2 */
static volatile uint8_t q_head;
static volatile uint8_t q_len;
```

**Enqueue** (ISR-safe):
```c
uint8_t idx = (q_head + q_len) % 32;
queue[idx] = {ev, data, target};
q_len++;
```

**Dequeue** (main loop only):
```c
ev = queue[q_head].ev;
q_head = (q_head + 1) % 32;
q_len--;
```

No locks needed: `q_head` written only by main, `q_len` is atomic
on MSP430 (8-bit write).

### 8.3 Timer Subsystem

The timer module is itself a process (`tiku_timer_process`):

```
  Clock ISR (128 Hz)
       |
       | tiku_timer_request_poll()
       v
  [POLL event in queue]
       |
       v
  Timer process wakes up
       |
       v
  Scan timer_list for expired timers
       |
  +----+----+
  |         |
  v         v
CALLBACK  EVENT
  mode      mode
  |         |
  v         v
call fn   post TIKU_EVENT_TIMER
           to target process
```

Timer list is a singly-linked list. `timer_is_due()` uses
wraparound-safe unsigned arithmetic:

```c
(tiku_clock_time_t)(now - t->start) >= t->interval
```

### 8.4 Scheduler Main Loop

```c
void tiku_sched_loop(void)
{
    tiku_autostart_start(tiku_autostart_processes);
    __enable_interrupt();

    while (sched_state == RUNNING) {
        /* Drain all pending work */
        while (tiku_sched_run_once()) {}

        /* Enter idle atomically */
        tiku_atomic_enter();
        if (!tiku_sched_has_pending()) {
            if (idle_hook) idle_hook();
        }
        tiku_atomic_exit();
    }
}
```

The atomic section prevents a race where an ISR fires between
the "is there work?" check and the low-power entry.

### 8.5 Memory Architecture

```
  tiku_mem_init()
       |
       +-- tiku_region_init()   Load platform memory map
       |     (SRAM, NVM, peripherals)
       |
       +-- tiku_mpu_init()      Configure write protection
       |     (SAM = 0x0555: R+X on all segments)
       |
       +-- tiku_mem_arch_init() Platform-specific setup
```

**Arena:** Bump pointer, O(1) alloc, O(1) reset (no individual free)

**Pool:** Embedded freelist, O(1) alloc and free, fixed block size

**Persist:** Key-value store backed by FRAM. Entries survive reboot.
Write count tracked for wear monitoring.

---

## Part 9: Platform Abstraction

### 9.1 Device / Board Split

```
arch/msp430/
  devices/
    tiku_device_fr5969.h      Silicon constants (memory sizes, pins)
    tiku_device_fr5994.h
    tiku_device_fr2433.h
  boards/
    tiku_board_fr5969_launchpad.h   GPIO assignments (LEDs, buttons)
    tiku_board_fr5994_launchpad.h
    tiku_board_fr2433_launchpad.h
  tiku_device_select.h        Routes TIKU_DEVICE_* to correct headers
```

**Adding a new MSP430 variant:**
1. Create `devices/tiku_device_frXXXX.h` (memory sizes, clock pins)
2. Create `boards/tiku_board_frXXXX_myboard.h` (LED/button GPIO)
3. Add `#elif` in `tiku_device_select.h`
4. Add linker script `lnk_msp430frXXXX.cmd`

### 9.2 HAL Layer

```
hal/
  tiku_cpu.h          Atomic sections, boot init
  tiku_clock_hal.h    Clock time, wait, delay
  tiku_watchdog_hal.h Kick, pause, resume
  tiku_mem_hal.h      NVM read/write, secure wipe
  tiku_mpu_hal.h      Segment permissions, violation flags
  tiku_region_hal.h   Platform memory table
  tiku_htimer_hal.h   Hardware timer schedule/cancel
```

---

## Part 10: TikuKits Libraries

TikuKits provides reusable, statically-allocated libraries:

| Library | Sub-modules | Description |
|---------|------------|-------------|
| **maths/** | matrix, statistics, distance | Linear algebra, streaming stats, distance metrics |
| **ml/** | linreg, logreg, dtree, knn, nbayes, linsvm, tnn | 7 ML algorithms, fixed-point, no FPU |
| **ds/** | array, ringbuf, stack, queue, pqueue, list, htable, bitmap, sortarray, btree, sm, bloom, circlog, deque, trie | 15 data structures |
| **sensors/** | mcp9808, adt7410, ds18b20 | Temperature sensor drivers |
| **sigfeatures/** | peak, zcr, histogram, delta, goertzel, zscore, scale | Signal feature extraction |
| **textcompression/** | rle, bpe, heatshrink | 3 compression algorithms |
| **net/** | slip, ipv4, icmp, udp, tftp | IPv4 networking stack |
| **time/** | ntp | NTP client |

### Naming Convention

```c
tiku_kits_<library>_<action>()           /* Functions */
tiku_kits_<library>_name_t               /* Types */
TIKU_KITS_<LIBRARY>_NAME                 /* Constants */
```

### Example: Matrix Multiplication

```c
#include "tikukits/maths/linear_algebra/tiku_kits_matrix.h"

struct tiku_kits_matrix a, b, c;

tiku_kits_matrix_init(&a, 2, 2);
tiku_kits_matrix_set(&a, 0, 0, 256);  /* 1.0 in Q8 */
tiku_kits_matrix_set(&a, 1, 1, 256);  /* identity */

tiku_kits_matrix_mul(&a, &b, &c);     /* c = a * b */
```

---

## Part 11: Testing

TikuOS uses TikuBench, a hardware-in-the-loop test framework:

```bash
# Run scheduler tests
python3 TikuBench/tikubench/runner.py --category scheduler \
    --no-interactive --mcu msp430fr5969

# Run all ML tests
python3 TikuBench/tikubench/runner.py --category ml-linreg \
    --no-interactive

# Run UART loopback (needs echo host)
python3 TikuBench/tikubench/runner.py --category uart-loopback \
    --no-interactive --port /dev/ttyACM1
```

**Test structure:**
```c
void test_my_feature(void)
{
    TEST_GROUP_BEGIN("My Feature");
    TEST_ASSERT(condition, "description");
    TEST_GROUP_END("My Feature");
}
```

See `presentations/tikubench_documentation.md` for the full TikuBench
reference.

---

## Appendix A: File Organization

```
tikuOS/
  main.c                      Entry point
  tiku.h                      Master system header
  Makefile                    Build system
  kernel/
    process/                  Process + protothread + channel
    scheduler/                Event-driven scheduler
    timers/                   Clock, software timers, hardware timers
    cpu/                      Watchdog, common utilities
    memory/                   Arena, pool, persist, region, MPU
  arch/msp430/                MSP430-specific implementations
  hal/                        Hardware abstraction layer
  boot/                       Boot sequence
  interfaces/                 I2C, ADC, 1-Wire drivers
  apps/cli/                   Interactive CLI application
  tikukits/                   Reusable library ecosystem
  tests/                      82 test files, 3,400 assertions
  TikuBench/                  Python test framework (47 files)
  examples/                   11 example applications
  docs/                       Documentation
  presentations/              Technical write-ups
```

## Appendix B: Coding Standards

- K&R brace style for control structures
- 4-space indentation (no tabs)
- 80-character line limit
- Function names: `tiku_module_action()`
- Types: `tiku_name_t`
- Constants: `TIKU_MODULE_NAME`
- Header guards: `TIKU_MODULE_H_`
- Static memory allocation only (no malloc)
- Doxygen documentation for public APIs
