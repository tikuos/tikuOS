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

## Supported Boards

| Board | MCU | RAM | FRAM |
|-------|-----|-----|------|
| MSP-EXP430FR2433 LaunchPad | MSP430FR2433 | 4 KB | 16 KB |
| MSP-EXP430FR5969 LaunchPad | MSP430FR5969 | 2 KB | 64 KB |
| MSP-EXP430FR5994 LaunchPad | MSP430FR5994 | 8 KB | 256 KB |

See [docs/platform.md](docs/platform.md) for detailed platform information, device/board differences, and how to add new variants.

---

## Quick Start

```bash
# Build for MSP430FR2433 (default)
make

# Build for a specific target
make MCU=msp430fr5969

# Build and flash
make flash MCU=msp430fr5969

# Open serial monitor
make monitor
```

See [docs/install.md](docs/install.md) for full prerequisites, build options, flashing, debugging, and test configuration.

---

## Minimal Example

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

## Project Structure

```
TikuOS/
├── arch/msp430/            # MSP430-specific implementations
│   ├── boards/             # Per-board GPIO/UART pin assignments
│   └── devices/            # Per-device silicon constants
├── kernel/                 # Platform-independent kernel
│   ├── process/            # Process & protothread management
│   ├── timers/             # Timer subsystems (clock, htimer, timer)
│   ├── cpu/                # CPU abstraction (boot, watchdog)
│   └── lib/                # Utility libraries
├── hal/                    # Hardware abstraction layer
├── examples/               # Ready-to-run example applications
├── tests/                  # Test suite
├── docs/                   # Documentation
├── tiku.h                  # Main configuration header
├── main.c                  # System entry point
└── Makefile                # Build system
```

---

## Documentation

- [docs/install.md](docs/install.md) - Installation, building, flashing, debugging, and testing
- [docs/platform.md](docs/platform.md) - Supported platforms, device selection, and adding new variants

---

## License

Apache License, Version 2.0

Copyright (c) 2025-2026 Ambuj Varshney

---

## Contact

- Website: [http://tiku-os.org](http://tiku-os.org)
- Author: Ambuj Varshney <ambuj@tiku-os.org>
