# Installation and Build Guide

## Prerequisites

### MSP430-GCC Toolchain

TikuOS uses the MSP430-GCC open-source compiler. The Makefile expects it installed at `~/tigcc`:

```
~/tigcc/bin/msp430-elf-gcc
~/tigcc/bin/msp430-elf-gdb
```

Download MSP430-GCC from [TI's MSP430-GCC page](https://www.ti.com/tool/MSP430-GCC-OPENSOURCE).

To install to a different location, update `TOOLCHAIN_DIR` in the Makefile.

### mspdebug (Flash/Debug Tool)

Install `mspdebug` for flashing and on-chip debugging:

```bash
# Ubuntu/Debian
sudo apt install mspdebug

# macOS
brew install mspdebug
```

The default debug driver is `tilib` (TI MSP Debug Stack). Some LaunchPads may need `ezfet` instead -- pass `DEBUGGER=ezfet` on the make command line.

### Serial Monitor (optional)

For viewing UART output, install `picocom` or `screen`:

```bash
sudo apt install picocom
```

---

## Building

```bash
# Build for the default target (MSP430FR2433)
make

# Build for a specific MCU
make MCU=msp430fr5969
make MCU=msp430fr5994
```

The MCU name is case-insensitive. The build output is `main.elf` in the project root, with object files placed under `build/<mcu>/`.

### Build Flags

The Makefile automatically derives the correct device define from the MCU name. For example, `MCU=msp430fr5969` passes `-DTIKU_DEVICE_MSP430FR5969=1` to the compiler. No manual editing of `tiku.h` is needed when building via Make.

### Cleaning

```bash
make clean
```

This removes the `build/` directory and `main.elf`.

---

## Flashing

```bash
# Build and flash the default target
make flash

# Build and flash a specific target
make flash MCU=msp430fr5969

# Use a different debugger driver
make flash MCU=msp430fr5969 DEBUGGER=ezfet
```

`make run` is an alias for `make flash`.

### Erasing

```bash
make erase MCU=msp430fr5969
```

---

## Debugging

Start a GDB server in one terminal, then connect from a second:

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

---

## Serial Monitor

Open a serial console to view UART debug output (default 9600 baud):

```bash
# Auto-detect LaunchPad serial port
make monitor

# Explicit port and baud rate
make monitor PORT=/dev/ttyACM0 BAUD=9600
```

Exit: `Ctrl-A Ctrl-X` (picocom) or `Ctrl-A k` (screen).

---

## Running Tests

1. Open `tests/tiku_test_config.h` and set the master flag:

   ```c
   #define TEST_ENABLE 1
   ```

2. Enable the specific test modules you want to run:

   ```c
   #define TEST_WATCHDOG 1
   #define TEST_WDT_BASIC 1
   ```

3. Build and flash as usual. The test runner (`tests/test_runner.c`) is called from `main.c` and dispatches to all enabled tests.

### Available Test Modules

| Flag | Description |
|------|-------------|
| `TEST_WATCHDOG` | Watchdog timer tests (`TEST_WDT_BASIC`, `TEST_WDT_PAUSE_RESUME`, `TEST_WDT_INTERVAL`, `TEST_WDT_TIMEOUT`) |
| `TEST_CPUCLOCK` | CPU clock configuration tests |
| `TEST_PROCESS` | Process/protothread tests (auto-derived from `TEST_PROCESS_LIFECYCLE`, `TEST_PROCESS_EVENTS`, `TEST_PROCESS_YIELD`, `TEST_PROCESS_BROADCAST`, `TEST_PROCESS_POLL`) |
| `TEST_TIMER` | Timer subsystem tests (`TEST_TIMER_EVENT`, `TEST_TIMER_CALLBACK`, `TEST_TIMER_PERIODIC`, `TEST_TIMER_STOP`, `TEST_HTIMER_BASIC`, `TEST_HTIMER_PERIODIC`) |

---

## Other Useful Commands

```bash
make size       # Show code/data size summary for the current build
```

---

## Debug Output

Enable per-subsystem debug printing by setting flags in `tiku.h`:

```c
#define DEBUG_PROCESS   1   // Process management
#define DEBUG_HTIMER    1   // Hardware timer
#define DEBUG_TIMER     1   // Event timer
#define DEBUG_CPU_FREQ  1   // CPU frequency
#define DEBUG_CLOCK_ARCH 1  // Clock architecture
#define DEBUG_SCHED     1   // Scheduler
#define DEBUG_WDT       1   // Watchdog timer
#define DEBUG_MAIN      1   // Main application
#define DEBUG_TESTS     1   // Test modules
```

Under GCC builds, debug output is sent via the backchannel UART (view with `make monitor`). Under CCS, output routes through JTAG semihosting.
