# TikuOS Shell (`tikushell`) — Definitive Guide

**Version 0.03**

The TikuOS shell is an interactive command-line service that runs as a kernel
process. It is transport-agnostic — the same command set works over UART,
telnet, or any custom backend implementing `tiku_shell_io_t`. UART at
**9600 baud, 8N1** is the default backend.

The shell is opt-in but enabled by default in the root `Makefile`
(`TIKU_SHELL_ENABLE ?= 1`).

---

## Table of Contents

- [Building](#building)
- [Connecting](#connecting)
- [Color Mode](#color-mode)
- [Line Editor and Limits](#line-editor-and-limits)
- [Command Reference](#command-reference)
  - [System](#system)
  - [Processes](#processes)
  - [Filesystem (VFS)](#filesystem-vfs)
  - [Hardware](#hardware)
  - [Power](#power)
  - [Boot](#boot)
- [Opt-in Commands](#opt-in-commands)
- [Aliases](#aliases)
- [Scheduled Jobs (`every` / `once` / `jobs`)](#scheduled-jobs)
- [Reactive Rules (`on` / `rules`)](#reactive-rules)
- [Smoke Test Sequence](#smoke-test-sequence)
- [Adding a New Command](#adding-a-new-command)
- [Memory Footprint](#memory-footprint)

---

## Building

Build from the project root:

```bash
make MCU=msp430fr5969                       # shell + tests (default)
make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1   # explicit
make MCU=msp430fr5969 TIKU_SHELL_COLOR=1    # ANSI colour output
make APP=cli MCU=msp430fr5969               # legacy: shell only, no tests/examples
make MCU=msp430fr5969 HAS_EXAMPLES=1        # shell + examples
make clean MCU=msp430fr5969                 # wipe build artifacts
```

`MEMORY_MODEL=small` is the default — text and rodata fit in the lower-FRAM
region (0x4400–0xFF7F, ~48 KB) and ISRs in the 0xFF80 vector table reach
their handlers via 16-bit pointers. There is an experimental
`MEMORY_MODEL=large` path (`-mlarge -mcode-region=either -mdata-region=either`)
that unlocks the chip's full 64 KB FRAM, but it currently requires every
ISR to carry an explicit `__attribute__((lower))` so the vector table can
still reach them — without that audit, large mode boots into a non-
responsive state. Treat as experimental until the ISR pass lands.

Convenience targets defined in the root `Makefile`:

```bash
make flash   MCU=msp430fr5969               # compile + flash via mspdebug
make debug   MCU=msp430fr5969               # compile + start GDB server
make erase   MCU=msp430fr5969               # erase chip
make monitor                                # open serial console (auto-detect)
make monitor PORT=/dev/ttyACM1 BAUD=9600    # explicit port/baud
```

Build output: `build/<mcu>/TikuOS.out`.

---

## Connecting

After flashing, attach a serial terminal to the eZ-FET backchannel UART:

| Setting   | Value      |
|-----------|------------|
| Baud rate | 9600       |
| Framing   | 8N1        |
| Flow ctrl | None       |

Examples:

```bash
make monitor                                # auto-detected port
picocom -b 9600 /dev/ttyACM1
screen /dev/ttyACM1 9600
minicom -D /dev/ttyACM1 -b 9600
```

You should see the boot banner followed by the `tikuOS>` prompt:

```
  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/  v0.03
  Simple. Ubiquitous. Intelligence, Everywhere.

  MSP430FR5969  |  SRAM 2048B  FRAM 64KB
  Type 'help' for commands.

tikuOS>
```

### Optional TCP (telnet) backend

Build with the TCP backend on port 23:

```bash
make APP=cli MCU=msp430fr5969 \
     EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_SHELL_TCP_ENABLE=1"
```

Connect with `telnet <device-ip> 23`. The banner is sent only after a client
connects; multiple sequential clients are supported (one at a time).

---

## Color Mode

The shell can emit ANSI colour escapes for the banner, the prompt, and the
`help` / `free` / `tree` / status messages. Disabled by default to keep raw
serial logs clean.

Enable at build time:

```bash
make MCU=msp430fr5969 TIKU_SHELL_COLOR=1
```

This sets `-DTIKU_SHELL_COLOR=1` in `CFLAGS` (Makefile line ~227). Internally
it activates the ANSI macros in `kernel/shell/tiku_shell_config.h`:

| Macro       | Code     | Use                  |
|-------------|----------|----------------------|
| `SH_RST`    | `\033[0m` | Reset attributes    |
| `SH_BOLD`   | `\033[1m` | Bold / bright       |
| `SH_DIM`    | `\033[2m` | Dim / faint         |
| `SH_RED`    | `\033[31m` | Errors             |
| `SH_GREEN`  | `\033[32m` | Prompt / runtime   |
| `SH_YELLOW` | `\033[33m` | Compile-time info  |
| `SH_BLUE`   | `\033[34m` |                    |
| `SH_MAGENTA`| `\033[35m` |                    |
| `SH_CYAN`   | `\033[36m` | Banner / sections  |
| `SH_WHITE`  | `\033[37m` |                    |

When colour is off, every macro expands to an empty string — there is no
runtime cost.

**Terminal compatibility:** any modern serial terminal renders ANSI fine
(`picocom`, `screen`, `minicom`, PuTTY, telnet, GNOME Terminal). For raw
logging or non-ANSI consumers, leave colour disabled.

---

## Line Editor and Limits

Defined in `kernel/shell/tiku_shell.h`:

| Macro                    | Default | Meaning                              |
|--------------------------|---------|--------------------------------------|
| `TIKU_SHELL_LINE_SIZE`   | 64      | Max command line length (incl. NUL) |
| `TIKU_SHELL_MAX_ARGS`    | 8       | Max tokens per command line          |
| `TIKU_SHELL_POLL_TICKS`  | 1/20 s  | Input poll interval                  |

Editing keys:

| Key            | Effect                                                       |
|----------------|--------------------------------------------------------------|
| Printable      | Echo and append to the buffer                                |
| **Backspace** / **DEL** | Erase the previous character                        |
| **Enter** (CR/LF) | Submit the line                                           |
| **Ctrl+C** (0x03) | Abort current line, cancel every active job and rule, re-prompt — the escape hatch when an `every` job is flooding the screen |
| **Up arrow**   | Recall older history entry (replaces the visible line)        |
| **Down arrow** | Recall newer history entry; past newest, clears the line      |

Arrow keys arrive as the ANSI CSI sequence `ESC [ A` / `ESC [ B`. The shell
state-machines through the three bytes; `ESC O A` style sequences (sent by
some legacy terminals) are not recognised. Right/Left arrows are consumed
but ignored — there is no in-line cursor.

History recall pulls from the same FRAM-backed ring that `history` lists, so
it survives reboot. Typing a printable character on a recalled line "commits"
the recall (subsequent Up steps from the most recent again).

---

## Command Reference

The full list with categories matches the output of `help`. Default-on
commands are listed below; opt-in commands appear in their own
[section](#opt-in-commands).

### System

| Command   | Usage                       | Description                                  |
|-----------|-----------------------------|----------------------------------------------|
| `help`    | `help`                      | List all commands grouped by category        |
| `info`    | `info`                      | Device, CPU clock, uptime, queue, processes  |
| `free`    | `free`                      | Compile-time + runtime SRAM/FRAM usage       |
| `reboot`  | `reboot`                    | Watchdog-triggered system reset              |
| `history` | `history [N]`               | Last N commands from the FRAM ring (default 10) |
| `calc`    | `calc N [op N]...`          | Integer arithmetic with schoolbook precedence (`+ - * / % min max`) |
| `clear`   | `clear`                     | ANSI clear screen                            |

```text
tikuOS> info
Device:    MSP430FR5969
CPU:       8 MHz
Uptime:    0h 1m 24s (84 s)
Clock:     128 ticks/sec (now 10752)
Queue:     0/16 events
Processes: 3 registered

tikuOS> calc 2 + 3 * 4
  14

tikuOS> free
--- Compile-time ---
SRAM   2048 total
  .data+.bss    412
  reservd       512
FRAM  47104 total
  code        38912
  const/data   1248
  unallocd     6944
--- Runtime ---
SRAM
  stack now     228
  free now     1408
FRAM
  free now     6944
```

### Processes

| Command  | Usage                              | Description                              |
|----------|------------------------------------|------------------------------------------|
| `ps`     | `ps`                               | List processes (pid, state, SRAM, FRAM, name) |
| `start`  | `start [name]`                     | List catalogue or launch/resume a process |
| `kill`   | `kill <pid>`                       | Stop a running process                   |
| `resume` | `resume <pid>`                     | Resume a stopped process                 |
| `queue`  | `queue`                            | List pending events in the global queue  |
| `timer`  | `timer`                            | Show software timer subsystem status     |
| `every`  | `every <seconds> <cmd...>`         | Schedule a recurring command             |
| `once`   | `once <seconds> <cmd...>`          | Schedule a one-shot command              |
| `jobs`   | `jobs` / `jobs del <id>` / `jobs del all` | List or delete scheduled jobs     |
| `on`     | `on <path> <op> <value> <cmd...>`  | Register a reactive rule                 |
|          | `on changed <path> <cmd...>`       | Fire whenever a VFS node changes         |
| `rules`  | `rules` / `rules del <id>`         | List or delete reactive rules            |

```text
tikuOS> ps
PID  STATE     SRAM  FRAM  NAME
---  --------  ----  ----  ---------------
  1  RUNNING     32     0  Shell
  2  WAITING     16     0  Blink
  3  WAITING     24     0  Net
---
3 process(es) registered
Event queue: 0/16

tikuOS> start
Available processes:
  Blink         running (pid 2)
  Sensor        not started

tikuOS> start Sensor
Started 'Sensor' (pid 4)
```

### Filesystem (VFS)

The shell exposes the in-kernel VFS at `/`. Notable trees:

- `/dev/`  — devices (`led0`, `button0`, `adc/temp`, …)
- `/proc/` — process inspection (`/proc/<pid>/state`, `…/uptime`)
- `/sys/`  — kernel state (`/sys/timer/count`, `/sys/clock/now`)

| Command   | Usage                                  | Description                            |
|-----------|----------------------------------------|----------------------------------------|
| `ls`      | `ls [path]`                            | List directory entries                 |
| `tree`    | `tree [path]`                          | Recursive directory dump               |
| `cd`      | `cd <path>`                            | Change working directory               |
| `pwd`     | `pwd`                                  | Print working directory                |
| `read`    | `read <path>`                          | Read a VFS node                        |
| `cat`     | `cat <path>`                           | Alias for `read`                       |
| `watch`   | `watch <path> [interval]`              | Periodic read until Ctrl+C             |
| `changed` | `changed <path>`                       | Block until a VFS node changes         |
| `write`   | `write <path> <value>`                 | Write a VFS node                       |
| `toggle`  | `toggle <path>`                        | Flip a binary (0/1) VFS node           |
| `name`    | `name [<new-name>]`                    | Read or set device name                |
| `if`      | `if <path> <op> <value> <cmd...>`      | One-shot conditional (opt-in — see [Opt-in](#opt-in-commands)) |
| `irq`     | `irq P<port>.<pin> <rising\|falling\|both\|off>` | Configure GPIO edge IRQ → event |
| `alias`   | `alias [<name> <body...>]`             | Define / list FRAM-backed aliases      |
| `unalias` | `unalias <name>`                       | Remove an alias                        |
| `echo`    | `echo <args...>`                       | Print arguments + newline              |

Comparison operators for `if` and `on`: `==`, `!=`, `<`, `>`, `<=`, `>=`. Note the **double** equals — single `=` is rejected with `unknown operator '='`.

```text
tikuOS> ls /dev
led0      led1      button0   adc/      uart0

tikuOS> read /dev/led0
0
tikuOS> write /dev/led0 1
tikuOS> toggle /dev/led0
tikuOS> read /dev/led0
0

tikuOS> watch /dev/adc/temp 2
27.4 C
27.4 C
27.5 C
^C

tikuOS> if /dev/button0 == 1 toggle /dev/led0

tikuOS> tree /proc
/proc
├── 1
│   ├── name
│   ├── state
│   └── uptime
├── 2
│   ├── ...
```

### Hardware

| Command | Usage                                      | Description                              |
|---------|--------------------------------------------|------------------------------------------|
| `gpio`  | `gpio <port> <pin> [0\|1\|t\|in]`          | Read/write/toggle GPIO; `in` = input     |
| `adc`   | `adc <channel\|temp\|bat> [ref]`           | Sample analog channel (or built-in temp/Vbat) |

```text
tikuOS> gpio 1 0          # read P1.0
P1.0 = 0
tikuOS> gpio 1 0 1        # drive high
tikuOS> gpio 1 0 t        # toggle
tikuOS> gpio 1 0 in       # configure as input

tikuOS> adc temp
Temp = 27.6 C
tikuOS> adc bat
Vbat = 3287 mV
tikuOS> adc 5             # raw channel 5
ADC[5] = 0x1A2C
```

### Power

| Command | Usage                              | Description                                |
|---------|------------------------------------|--------------------------------------------|
| `sleep` | `sleep <off\|lpm0\|lpm3\|lpm4>`    | Set the idle low-power mode                |
| `wake`  | `wake`                             | Show enabled wake sources for each LPM     |

```text
tikuOS> sleep lpm3
sleep mode: LPM3

tikuOS> wake
Wake sources:
  Timer A0 (sys clock)  [on ]  wakes LPM0-3
  Timer A1 (htimer)     [off]  wakes LPM0-3
  UART RX  (eUSCI_A0)   [on ]  wakes LPM0
  Watchdog (interval)   [off]  wakes LPM0-3
  GPIO IRQ              [off]  wakes LPM0-4

Note: LPM4 disables all clocks.
  Only GPIO IRQ can wake from LPM4.
```

### Boot

Available only when `TIKU_INIT_ENABLE=1` (off by default).

| Command | Usage                              | Description                              |
|---------|------------------------------------|------------------------------------------|
| `init`  | `init <list\|add\|rm\|enable\|disable\|run>` | Manage FRAM-persistent boot entries |
|         | `init add <seq> <name> <cmd...>`   | Add a boot entry at sequence number      |
|         | `init rm <name>`                   | Remove a boot entry                      |
|         | `init enable <name>`               | Enable an entry                          |
|         | `init disable <name>`              | Disable an entry                         |
|         | `init run`                         | Run all enabled entries now              |

---

## Opt-in Commands

Disabled by default to keep the FR5969 build under the 48 KB lower-FRAM cap.
Enable via `EXTRA_CFLAGS="-DTIKU_SHELL_CMD_<NAME>=1"`. The larger ones
(`i2c`, `peek`, `poke`, `if`) need a paired disable of a comparable command.

| Command  | Flag                        | Usage                                    |
|----------|-----------------------------|------------------------------------------|
| `if`     | `TIKU_SHELL_CMD_IF`         | `if <path> <op> <value> <command...>` (off by default — see [Memory Footprint](#memory-footprint); use `on` rules instead) |
| `i2c`    | `TIKU_SHELL_CMD_I2C`        | `i2c scan` / `i2c read <addr> <count>` / `i2c write <addr> <byte>...` |
| `peek`   | `TIKU_SHELL_CMD_PEEK`       | `peek <addr> [count]`                   |
| `poke`   | `TIKU_SHELL_CMD_POKE`       | `poke <addr> <byte>`                    |
| `delay`  | `TIKU_SHELL_CMD_DELAY`      | `delay <ms>` (synchronous wait, no LPM) |
| `repeat` | `TIKU_SHELL_CMD_REPEAT`     | `repeat <count> <command...>`           |

Examples:

```bash
make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
     EXTRA_CFLAGS="-DTIKU_SHELL_CMD_DELAY=1 -DTIKU_SHELL_CMD_REPEAT=1"

make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
     EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1 -DTIKU_SHELL_CMD_HISTORY=0"

make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
     EXTRA_CFLAGS="-DTIKU_SHELL_CMD_PEEK=1 -DTIKU_SHELL_CMD_POKE=1 -DTIKU_SHELL_CMD_HISTORY=0"
```

Disable a default-on command the same way: `-DTIKU_SHELL_CMD_CALC=0`.

---

## Aliases

Aliases are short names that expand to a full command, persisted in FRAM so
they survive reboot.

```text
tikuOS> alias l ls /dev
aliased 'l'
tikuOS> alias                    # list
  l = ls /dev
tikuOS> l
led0  led1  button0  adc/  uart0
tikuOS> unalias l
```

Alias bodies may include arguments and may invoke other aliases (one level of
expansion per dispatch — no recursion).

---

## Scheduled Jobs

`every` and `once` register jobs that the shell fires during its tick. Both
dispatch through the same parser as interactive commands. Successful
scheduling is **silent** — the prompt simply returns. Job IDs are slot
indices and start at `#0`; the recurring jobs print as `[active]`, one-shots
as `[pending]`.

```text
tikuOS> every 5 toggle /dev/led0     # blink every 5 s
tikuOS> once 30 reboot               # reboot in 30 s
tikuOS> jobs
  #0  every 5s    toggle /dev/led0   [active]
  #1  once  30s   reboot             [pending]
tikuOS> jobs del 0
Deleted job #0
tikuOS> jobs del all                 # nuke everything left
Deleted 1 job
```

If an `every` job is flooding the prompt, **Ctrl+C** clears every active job
and rule in one keystroke (see [Line Editor](#line-editor-and-limits)).

Jobs are stored in SRAM and lost on reboot. For boot-persistent commands use
the `init` system instead.

---

## Reactive Rules

`on` registers a rule that watches a VFS path and fires a command on each
**false → true** transition (edge-triggered, not level). Successful
registration is silent. Rule IDs are slot indices and start at `#0`.

```text
tikuOS> on /dev/button0 == 1 toggle /dev/led0
tikuOS> on changed /dev/adc/temp echo temp updated
tikuOS> rules
  #0  on /dev/button0 == 1    -> toggle /dev/led0
  #1  on changed /dev/adc/temp -> echo temp updated
tikuOS> rules del 0
Deleted rule #0
```

Rules and jobs combine cleanly — for example, an `every 1 read /dev/adc/temp`
job will trigger an `on changed /dev/adc/temp ...` rule whenever the value
moves.

---

## Smoke Test Sequence

After connecting, paste these to exercise every subsystem:

```text
help
info
free
ps
ls /
tree /dev
read /dev/led0
write /dev/led0 1
toggle /dev/led0
gpio 1 0 t
adc temp
calc 2 + 3 * 4
every 2 toggle /dev/led0
jobs
jobs del all
on /dev/button0 == 1 toggle /dev/led0
rules
rules del 0
alias l ls /dev
l
unalias l
history 10
wake
sleep lpm3
sleep off
reboot
```

Every line above is expected to succeed on a default-build FR5969 LaunchPad.
Errors on any line indicate a misconfiguration to investigate.

---

## Host-Side Test Suite

A Python integration suite at `TikuBench/tikubench/shell/` drives the
shell over UART and validates everything that the on-target C tests
cannot reach: the line editor, prompt/banner output, Ctrl+C, up/down
arrow recall, FRAM-backed history persistence across reboot, and the
poll-loop dispatch of jobs and rules.

```bash
# Build APP=cli, flash, run all 25 tests
python3 TikuBench/tikubench/shell_test.py --port /dev/ttyACM0

# List discovered tests
python3 -m tikubench.shell --list

# Run only history/arrow tests
python3 -m tikubench.shell --test 20 21 22 95

# Through the unified runner with category filter
python3 TikuBench/tikubench/runner.py --category shell-integration --no-interactive
python3 TikuBench/tikubench/runner.py --category shell-history     --no-interactive
python3 TikuBench/tikubench/runner.py --category shell-jobs        --no-interactive
```

Full architecture, conventions, and test inventory:
[`TikuBench/docs/SHELL.md`](../../TikuBench/docs/SHELL.md).

---

## Adding a New Command

See `CLAUDE.md` for the canonical four-step recipe. In short:

1. Add a `TIKU_SHELL_CMD_XXX` flag to `kernel/shell/tiku_shell_config.h`.
2. Create `kernel/shell/commands/tiku_shell_cmd_xxx.h` and `.c`.
3. `#include` the header and add a `{name, help, handler}` row to
   `tiku_shell_commands[]` in `kernel/shell/tiku_shell.c`.
4. Add the `.c` to the `ifeq ($(TIKU_SHELL_ENABLE),1)` block in `Makefile`.

A handler signature is:

```c
void tiku_shell_cmd_xxx(uint8_t argc, const char *argv[]);
```

`argv[0]` is the command name; emit output with `SHELL_PRINTF(...)`.

---

## Memory Footprint

Approximate cost on FR5969 with the default command set:

| Region | Used by shell | Notes                                |
|--------|---------------|--------------------------------------|
| SRAM   | ~120 B        | line buffer + parser state + timer + history-recall state |
| FRAM   | ~6 KB         | code + history ring + alias table    |

The default FR5969 build sits at **47.2 KB / 48 KB** of the lower-FRAM cap
(0x4400–0xFF7F). Crossing the cap pushes libnosys/libc into HIFRAM where
their 16-bit relocations cannot reach, and the link fails with
`R_MSP430X_ABS16` truncation errors. To stay under the cap, the default
shell turns **`if` off** — its 1 KB of code is replaced for most workflows
by `on` (rules), which is far more powerful (edge-triggered, persistent
across the tick).

The chip itself has 64 KB of FRAM (lower 48 KB at 0x4400 + upper 16 KB at
0x10000). Unlocking the upper region needs `MEMORY_MODEL=large`
(`-mlarge -mcode-region=either -mdata-region=either`) **plus** an
`__attribute__((lower))` audit of every ISR — without that, ISRs can be
linked into HIFRAM where the 0xFF80 vector table's 16-bit pointers
truncate them and the device boots into a non-responsive state. Treat the
large-mode build as experimental until that audit lands.

Adjust the recipe with `EXTRA_CFLAGS`:

```bash
# Bring `if` back, drop something comparable in size:
make MCU=msp430fr5969 \
     EXTRA_CFLAGS="-DTIKU_SHELL_CMD_IF=1 -DTIKU_SHELL_CMD_CALC=0"

# Drop arrow-key navigation entirely (saves ~270 B):
make MCU=msp430fr5969 \
     EXTRA_CFLAGS="-DTIKU_SHELL_CMD_IF=1 -DTIKU_SHELL_CMD_HISTORY=0"
```

Approximate sizes of the larger optional bits, for swap planning:

| Component        | FRAM (~) |
|------------------|---------:|
| `calc`           | 1.3 KB   |
| `if`             | 1.0 KB   |
| `free`           | 0.9 KB   |
| `gpio`           | 0.8 KB   |
| `history` ring   | 1.0 KB (data) + 0.7 KB (code) |
| arrow-key recall | 0.3 KB   |
