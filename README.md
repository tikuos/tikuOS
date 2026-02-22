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

---

## Prerequisites

### Toolchain

TikuOS uses the MSP430-GCC open-source compiler. Install it to `~/tigcc` (or update `TOOLCHAIN_DIR` in the Makefile):

```bash
# The Makefile expects the toolchain at:
#   ~/tigcc/bin/msp430-elf-gcc
#   ~/tigcc/bin/msp430-elf-gdb
```

Download MSP430-GCC from [TI's MSP430-GCC page](https://www.ti.com/tool/MSP430-GCC-OPENSOURCE).

### Flash/Debug Tool

Install `mspdebug` for flashing and debugging:

```bash
# Ubuntu/Debian
sudo apt install mspdebug

# macOS
brew install mspdebug
```

The default debug driver is `tilib` (TI MSP Debug Stack). For some LaunchPads you may need `ezfet` instead — pass `DEBUGGER=ezfet` on the make command line.

### Serial Monitor (optional)

For viewing UART output, install `picocom` or `screen`:

```bash
sudo apt install picocom
```

---

## Quick Start

### Building

```bash
# Build for MSP430FR2433 (default)
make

# Build for a specific target
make MCU=msp430fr5969
make MCU=msp430fr5994
```

The MCU name is case-insensitive. The output is `main.elf` in the project root.

### Flashing

```bash
# Build and flash
make flash

# Build and flash a specific target
make flash MCU=msp430fr5969

# Use a different debugger driver
make flash MCU=msp430fr5969 DEBUGGER=ezfet
```

### Serial Monitor

Open a serial console to view UART debug output (9600 baud):

```bash
# Auto-detect LaunchPad serial port
make monitor

# Explicit port and baud rate
make monitor PORT=/dev/ttyACM0 BAUD=9600
```

Exit: `Ctrl-A Ctrl-X` (picocom) or `Ctrl-A k` (screen).

### Debugging

Start a GDB server, then connect from a second terminal:

```bash
# Terminal 1: start GDB server
make debug MCU=msp430fr5969

# Terminal 2: connect with GDB
~/tigcc/bin/msp430-elf-gdb main.elf
(gdb) target remote :2000
(gdb) load
(gdb) break main
(gdb) continue
```

### Other Commands

```bash
make clean                      # Remove all build artifacts
make erase MCU=msp430fr5969     # Erase chip flash/FRAM
make size                       # Show code/data size summary
```

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
