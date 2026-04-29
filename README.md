<h1 align="center">
  <br>
  <pre>
  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/
  </pre>
  <br>
  <em>Simple. Ubiquitous. Intelligence, Everywhere.</em>
</h1>

<p align="center">
  <a href="#quick-start"><img src="https://img.shields.io/badge/build-make-blue?style=flat-square" alt="Build"></a>
  <a href="#supported-boards"><img src="https://img.shields.io/badge/MCU-MSP430FR-red?style=flat-square" alt="MCU"></a>
  <a href="#license"><img src="https://img.shields.io/badge/license-Apache%202.0-green?style=flat-square" alt="License"></a>
  <a href="#interactive-shell"><img src="https://img.shields.io/badge/shell-38%20commands-orange?style=flat-square" alt="Shell"></a>
  <a href="http://tiku-os.org"><img src="https://img.shields.io/badge/web-tiku--os.org-purple?style=flat-square" alt="Website"></a>
</p>

<p align="center">
  TikuOS is an operating system for <strong>microwatt computers</strong>: tiny, ubiquitous devices that run for years on a coin cell or indefinitely on harvested energy. It is the first OS designed for sub-milliwatt communication primitives, including backscatter and tunnel diode based beyond-backscatter transceivers, with IP networking and machine intelligence as integral parts of the operating system.
</p>

---

## Supported Boards

| Board | MCU | RAM | FRAM | Status |
|-------|-----|-----|------|--------|
| MSP-EXP430FR5969 LaunchPad | MSP430FR5969 | 2 KB | 64 KB | :green_circle: Primary |

---

## Quick Start

```bash
# Build for a specific target
make MCU=msp430fr5969

# Build and flash
make flash MCU=msp430fr5969

# Open serial monitor
make monitor
```

---

## :computer: Interactive Shell

TikuOS includes a full interactive shell over UART or Telnet. Control GPIO pins, read sensors, manage processes, configure boot sequences, and inspect memory — **all without recompiling**. Build with `TIKU_SHELL_COLOR=1` for ANSI color output.

```
  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/  v0.03
  Simple. Ubiquitous. Intelligence, Everywhere.

  MSP430FR5969  |  SRAM 2048B  FRAM 64KB
  Type 'help' for commands.

tikuOS> help
 --- System ---
  help       Show available commands
  info       Device, CPU, uptime, clock
  free       Memory usage (SRAM/FRAM)
  reboot     System reset
  history    Last N commands from FRAM
  calc       Integer arithmetic
  clear      Clear screen (ANSI)
 --- Processes ---
  ps         List active processes
  start      Start/resume by name
  kill       Stop a process (by pid)
  resume     Resume a stopped process
  queue      List pending events
  timer      Software timer status
  every      Schedule a recurring command
  once       Schedule a one-shot command
  jobs       List/delete scheduled jobs
  on         Register a reactive rule
  rules      List/delete reactive rules
 --- Filesystem ---
  ls         List directory
  tree       Recursive directory listing
  cd         Change directory
  pwd        Print working directory
  read       Read a VFS node
  watch      Read VFS node every N sec
  changed    Block until VFS node changes
  write      Write a VFS node
  name       Read/set device name
  irq        Enable/disable GPIO edge IRQ
  alias      Define/list FRAM-backed aliases
  unalias    Remove an alias
  toggle     Flip a binary VFS node
  cat        Read (alias)
  echo       Print arguments + newline
 --- Hardware ---
  gpio       Read/write GPIO pins
  adc        Read analog channel
 --- Power ---
  sleep      Set low-power idle mode
  wake       Show active wake sources
 --- Boot ---
  init       Manage FRAM boot entries
```

> :bulb: Opt-in extras (off by default; enable via `EXTRA_CFLAGS`): `if`
> (conditional), `i2c` (bus scan/read/write), `delay`, `repeat`, `peek`,
> `poke`. See `kernel/shell/tiku_shell_config.h` for the full list and
> rationale (each has a FRAM cost on FR5969).

> :art: Build with `TIKU_SHELL_COLOR=1` for ANSI colored output (cyan logo, green prompt, categorized help). Add a screenshot from picocom here.

---

### :zap: Configurable Boot Sequence (FRAM-backed)

> **Every other RTOS:** change boot behavior :arrow_right: recompile :arrow_right: reflash.
>
> **TikuOS:** change boot behavior :arrow_right: edit over shell :arrow_right: reboot.

```
tikuOS> init add 05 network start net
OK: 'network' at seq 05

tikuOS> init add 10 mqtt    start mqtt
OK: 'mqtt' at seq 10

tikuOS> init add 20 leds    write /dev/led0 1
OK: 'leds' at seq 20

tikuOS> init list
 05  network      [on ]  start net
 10  mqtt         [on ]  start mqtt
 20  leds         [on ]  write /dev/led0 1
```

Boot entries are stored in **FRAM** — they survive power cycles without flash erase cycles. Same firmware, different boot sequences per device. Disable a service without removing it:

```
tikuOS> init disable mqtt
OK: disabled 'mqtt'

tikuOS> init list
 05  network      [on ]  start net
 10  mqtt         [off]  start mqtt
 20  leds         [on ]  write /dev/led0 1
```

Reboot. New behavior. No recompile. No reflash.

---

### :wrench: Hardware Debugging from the Shell

Toggle GPIO pins, read ADC channels, inspect memory — a live hardware debugging tool on a microcontroller.

```
tikuOS> gpio 4 6 t
P4.6 -> 1

tikuOS> gpio 4 6
P4.6 = 1 (output)

tikuOS> adc temp
Atemp = 1847 (0x737)

tikuOS> adc bat
Abat = 2048 (0x800)
```

---

### :bar_chart: Memory Introspection

Real numbers from linker symbols and the stack pointer — not placeholders.

```
tikuOS> free
--- Compile-time ---
SRAM   2048 total
  .data+.bss   1058
  reservd        990
FRAM  65535 total
  code        42508
  const/data   5620
  unallocd    17407
--- Runtime ---
SRAM
  stack now      80
  free now      910
FRAM
  free now    17407
  config rgn   1024 allocated
  init table     70 (1/8 entries)
--- Processes (1/8) ---
 pid  name        sram  fram  state
   0  Shell          0     0  running
```

---

### :battery: Power Management

Enter ultra-low-power modes from the shell. See what will wake you up.

```
tikuOS> sleep lpm3
Idle: LPM3

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

---

### :open_file_folder: Virtual Filesystem

A unified namespace for the entire system — peripherals, OS state, config, and processes are all paths, just like a desktop operating system. No other MCU RTOS does this. The same `read`/`write` interface works for LEDs, sensors, timers, processes, and everything else.

```
/
├── sys/
│   ├── version              "0.03"
│   ├── device/
│   │   ├── name             user-set device name (FRAM-backed, R/W)
│   │   ├── id               unique tiku-XXXX hostname-style ID
│   │   ├── mcu              silicon part number ("MSP430FR5969")
│   │   └── version          OS version string
│   ├── uptime               seconds since boot
│   ├── mem/
│   │   ├── sram             RAM size in bytes
│   │   ├── nvm              FRAM size in bytes
│   │   ├── free             live stack headroom (SP - BSS end)
│   │   └── used             sum of per-process SRAM allocation
│   ├── cpu/
│   │   └── freq             clock Hz (8000000)
│   ├── power/
│   │   ├── mode             current LPM (off/LPM0/LPM3/LPM4)
│   │   └── wake             active wake sources
│   ├── timer/
│   │   ├── count            active software timers
│   │   ├── next             ticks until next expiration
│   │   ├── fired            total expirations since boot
│   │   └── list/{0..3}      per-timer mode, remaining, interval
│   ├── clock/
│   │   └── ticks            raw tick counter
│   ├── watchdog/
│   │   ├── mode             R/W "watchdog" or "interval"
│   │   ├── clock            R/W "aclk" or "smclk"
│   │   ├── interval         R/W divider (64/512/8192/32768)
│   │   └── kick             W   write to kick the timer
│   ├── htimer/
│   │   ├── now              hardware timer counter
│   │   └── scheduled        1 if pending, 0 if idle
│   ├── boot/
│   │   ├── reason           last reset cause (brownout/wdt/rstnmi/...)
│   │   ├── count            hibernate boot counter
│   │   ├── stage            boot stage (init/cpu/.../complete)
│   │   ├── rstiv            raw SYSRSTIV hex (0x0016, ...)
│   │   ├── clock/
│   │   │   ├── mclk         live MCLK frequency in Hz
│   │   │   ├── smclk        live SMCLK frequency in Hz
│   │   │   ├── aclk         live ACLK frequency in Hz
│   │   │   └── fault        clock fault flag (0 or 1)
│   │   └── mpu/
│   │       └── violations   MPU violation flags (hex bitmask)
│   └── sched/
│       └── idle             scheduler idle entry count
├── dev/
│   ├── led0                 read/write (0, 1, t=toggle)
│   ├── led1                 read/write
│   ├── console              R/W system console (UART)
│   ├── null                 R/W data sink (always empty on read)
│   ├── zero                 R   zero source (fills buffer with NUL)
│   ├── gpio/{1..4}/{0..7}   per-pin read/write (0, 1, t, i=input)
│   ├── gpio_dir/{1..4}      per-port pin direction (I=input, O=output)
│   ├── uart/
│   │   ├── overruns         UART overrun count since boot
│   │   └── baud             configured baud rate
│   ├── adc/
│   │   ├── temp             on-chip temperature sensor (raw ADC)
│   │   └── battery          battery voltage (raw ADC)
│   ├── i2c/
│   │   └── scan             list responding I2C addresses
│   └── spi/
│       └── config           SPI mode, bit order, prescaler (or "n/a")
└── proc/
    ├── count                number of active processes
    ├── queue/
    │   ├── length           pending events in queue
    │   └── space            free event slots
    ├── catalog/
    │   ├── count            available-but-not-started processes
    │   └── {0..7}/name      catalog entry name
    └── {0..7}/              per-process directory
        ├── name             process name
        ├── state            running/ready/waiting/sleeping/stopped
        ├── pid              numeric process id
        ├── sram_used        SRAM bytes allocated
        ├── fram_used        FRAM bytes allocated
        ├── uptime           seconds since start
        ├── wake_count       times scheduled
        └── events           pending events for this process
```

```
tikuOS> cat /sys/version
0.03

tikuOS> cat /sys/device/mcu
MSP430FR5969

tikuOS> cat /sys/device/name
tiku

tikuOS> cat /sys/mem/free
752

tikuOS> cat /sys/timer/fired
1862

tikuOS> cat /sys/timer/list/0
evt rem=6 int=6

tikuOS> cat /sys/watchdog/mode
watchdog
tikuOS> write /sys/watchdog/interval 8192
tikuOS> cat /sys/watchdog/interval
8192

tikuOS> cat /sys/boot/reason
rstnmi
tikuOS> cat /sys/boot/rstiv
0x0004
tikuOS> cat /sys/boot/stage
complete
tikuOS> cat /sys/boot/clock/mclk
8000000
tikuOS> cat /sys/boot/clock/fault
0
tikuOS> cat /sys/boot/mpu/violations
0x00

tikuOS> cat /sys/sched/idle
0

tikuOS> cat /dev/uart/baud
115200

tikuOS> cat /dev/gpio_dir/1
OOOOOOOO

tikuOS> write /dev/console hello
tikuOS> write /dev/null anything

tikuOS> cat /proc/0/name
Shell

tikuOS> cat /proc/0/wake_count
2063

tikuOS> cat /proc/queue/space
31

tikuOS> ls /dev
  led0
  led1
  console
  null
  zero
  gpio/
  gpio_dir/
  uart/
  adc/
  i2c/
  spi/

tikuOS> write /dev/led0 1

tikuOS> cat /sys/cpu/freq
8000000
```

---

### Build Options

| Flag | Effect |
|------|--------|
| `TIKU_SHELL_ENABLE=1` | Enable interactive shell (UART) |
| `TIKU_INIT_ENABLE=1` | Enable FRAM-backed init system (implies shell) |
| `TIKU_SHELL_COLOR=1` | Enable ANSI color output (banner, prompt, help, free) |
| `UART_BAUD=115200` | Set UART baud rate (default 9600) |
| `MCU=msp430fr5969` | Target MCU |

```bash
# Shell with color output
make TIKU_SHELL_ENABLE=1 TIKU_SHELL_COLOR=1 MCU=msp430fr5969

# Shell + init system + color
make TIKU_INIT_ENABLE=1 TIKU_SHELL_COLOR=1 MCU=msp430fr5969

# Flash and connect
make flash MCU=msp430fr5969 && make monitor

# Connect over Telnet (requires TCP stack)
telnet 172.16.7.2
```

> :bulb: **Tip:** Color requires a terminal that renders ANSI escapes (picocom, screen, minicom, PuTTY). Disable for raw serial logging.

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

## Handbook

- **API References**
  - [Core Kernel API Reference](handbook/references/api-reference.md)

---

## License

Licensed under the Apache License, Version 2.0 (the "License"); you may not
use this software except in compliance with the License. You may obtain a copy
of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.

---

<p align="center">
  <a href="http://tiku-os.org">tiku-os.org</a> · <a href="mailto:ambuj@tiku-os.org">ambuj@tiku-os.org</a>
</p>
