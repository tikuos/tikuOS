# TikuOS

**Simple. Ubiquitous. Intelligence, Everywhere.**

TikuOS is an event-driven embedded operating system designed for ultra-low-power microcontrollers. Based on protothreads, it provides cooperative multitasking with minimal memory overhead, making it ideal for severely resource-constrained devices.

---

## Key Features

- **Event-Driven Architecture** - Processes communicate via a central event queue
- **Protothread-Based** - Stackless lightweight threads using only 2 bytes per thread
- **Ultra-Low Memory** - Runs on devices with as little as 2KB RAM
- **Static Allocation** - No dynamic memory (malloc/free) for predictable behavior
- **Power Efficient** - Designed for ultra-low-power modes (LPM0-4)
- **Hardware Abstraction** - Clean separation between kernel and platform code

## Target Platform

- **MSP430FR5969** - 16-bit RISC microcontroller with FRAM
- 2KB RAM, ~112KB FRAM
- Ultra-low-power operation

---

## Quick Start

### Building

```bash
cd Debug
make
```

### Minimal Example

```c
#include "tiku.h"

TIKU_PROCESS(blink_process, "Blink");

static struct tiku_timer timer;

TIKU_PROCESS_THREAD(blink_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_timer_set_event(&timer, TIKU_CLOCK_SECOND);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_common_led1_toggle();
        tiku_timer_reset(&timer);
    }

    TIKU_PROCESS_END();
}

TIKU_AUTOSTART_PROCESSES(&blink_process);
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](docs/GettingStarted.md) | Setup guide and first steps |
| [Examples](docs/Examples.md) | Ready-to-run example applications |
| [Architecture](docs/Architecture.md) | System design and internals |
| [API Reference](docs/APIReference.md) | Complete API documentation |
| [Coding Convention](docs/CodingConvention.md) | Code style guidelines |
| [Porting Guide](docs/PortingGuide.md) | How to port to new platforms |

---

## Project Structure

```
TikuOS/
├── kernel/                 # Platform-independent kernel
│   ├── process/            # Process & protothread management
│   ├── timers/             # Timer subsystems (clock, htimer, timer)
│   ├── cpu/                # CPU abstraction (boot, watchdog)
│   └── lib/                # Utility libraries
├── arch/msp430/            # MSP430-specific implementations
├── hal/                    # Hardware abstraction layer
├── examples/               # Ready-to-run example applications
├── tests/                  # Test suite
├── docs/                   # Documentation
├── tiku.h                  # Main configuration header
└── main.c                  # System entry point
```

---

## Core Concepts

### Processes

Processes are lightweight execution units built on protothreads. They run cooperatively and communicate via events.

```c
TIKU_PROCESS(my_process, "My Process");

TIKU_PROCESS_THREAD(my_process, ev, data)
{
    TIKU_PROCESS_BEGIN();

    // Initialization code

    while (1) {
        TIKU_PROCESS_WAIT_EVENT();  // Wait for any event
        // Handle event
    }

    TIKU_PROCESS_END();
}
```

### Timers

Two timer types for different needs:

- **Software Timers** (`tiku_timer`) - Event or callback mode, ~8ms resolution
- **Hardware Timers** (`tiku_htimer`) - ISR-driven, microsecond precision

```c
// Event timer
static struct tiku_timer t;
tiku_timer_set_event(&t, TIKU_CLOCK_SECOND);

// Callback timer
tiku_timer_set_callback(&t, TIKU_CLOCK_SECOND, my_callback, NULL);

// Hardware timer (ISR context)
tiku_htimer_set(&ht, TIKU_HTIMER_NOW() + TIKU_HTIMER_SECOND, my_isr, NULL);
```

### Events

Processes communicate through typed events:

| Event | Description |
|-------|-------------|
| `TIKU_EVENT_INIT` | Process initialization |
| `TIKU_EVENT_TIMER` | Timer expiration |
| `TIKU_EVENT_POLL` | Poll request |
| `TIKU_EVENT_EXIT` | Process termination |

---

## Memory Requirements

| Component | RAM Usage |
|-----------|-----------|
| Per process | ~14 bytes |
| Per timer | ~20-24 bytes |
| Event queue (32 entries) | ~160 bytes |
| Stack | 256 bytes |

---

## License

Apache License, Version 2.0

Copyright (c) 2025-2026 Ambuj Varshney

---

## Contact

- Website: [http://tiku-os.org](http://tiku-os.org)
- Author: Ambuj Varshney <ambuj@tiku-os.org>
