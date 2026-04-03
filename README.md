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
  <a href="#interactive-shell"><img src="https://img.shields.io/badge/shell-22%20commands-orange?style=flat-square" alt="Shell"></a>
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
| MSP-EXP430FR5994 LaunchPad | MSP430FR5994 | 8 KB | 256 KB | :yellow_circle: Supported |
| MSP-EXP430FR2433 LaunchPad | MSP430FR2433 | 4 KB | 16 KB | :yellow_circle: Supported |

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

<table><tr><td>
<pre>
<span style="color:#06989a"><b>  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/</b></span>  <span style="color:#888">v0.01</span>
  <span style="color:#888">Simple. Ubiquitous. Intelligence, Everywhere.</span>

  <b>MSP430FR5969</b>  |  SRAM 2048B  FRAM 64KB
  <span style="color:#888">Type 'help' for commands.</span>

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> help
 <span style="color:#06989a">--- System ---</span>
  <b>help</b>       Show available commands
  <b>info</b>       Device, CPU, uptime, clock
  <b>free</b>       Memory usage (SRAM/FRAM)
  <b>reboot</b>     System reset
  <b>history</b>    Last N commands from FRAM
 <span style="color:#06989a">--- Processes ---</span>
  <b>ps</b>         List active processes
  <b>start</b>      Start/resume by name
  <b>kill</b>       Stop a process (by pid)
  <b>resume</b>     Resume a stopped process
  <b>queue</b>      List pending events
  <b>timer</b>      Software timer status
 <span style="color:#06989a">--- Filesystem ---</span>
  <b>ls</b>         List directory
  <b>cd</b>         Change directory
  <b>pwd</b>        Print working directory
  <b>read</b>       Read a VFS node
  <b>write</b>      Write a VFS node
  <b>toggle</b>     Flip a binary VFS node
  <b>cat</b>        Read (alias)
  <b>echo</b>       Write (alias)
 <span style="color:#06989a">--- Hardware ---</span>
  <b>gpio</b>       Read/write GPIO pins
  <b>adc</b>        Read analog channel
 <span style="color:#06989a">--- Power ---</span>
  <b>sleep</b>      Set low-power idle mode
  <b>wake</b>       Show active wake sources
 <span style="color:#06989a">--- Boot ---</span>
  <b>init</b>       Manage FRAM boot entries
</pre>
</td></tr></table>

---

### :zap: Configurable Boot Sequence (FRAM-backed)

> **Every other RTOS:** change boot behavior :arrow_right: recompile :arrow_right: reflash.
>
> **TikuOS:** change boot behavior :arrow_right: edit over shell :arrow_right: reboot.

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init add 05 network start net
OK: 'network' at seq 05

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init add 10 mqtt    start mqtt
OK: 'mqtt' at seq 10

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init add 20 leds    write /dev/led0 1
OK: 'leds' at seq 20

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init list
 05  network      [on ]  start net
 10  mqtt         [on ]  start mqtt
 20  leds         [on ]  write /dev/led0 1
</pre>
</td></tr></table>

Boot entries are stored in **FRAM** — they survive power cycles without flash erase cycles. Same firmware, different boot sequences per device. Disable a service without removing it:

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init disable mqtt
OK: disabled 'mqtt'

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> init list
 05  network      [on ]  start net
 10  mqtt         [off]  start mqtt
 20  leds         [on ]  write /dev/led0 1
</pre>
</td></tr></table>

Reboot. New behavior. No recompile. No reflash.

---

### :wrench: Hardware Debugging from the Shell

Toggle GPIO pins, read ADC channels, inspect memory — a live hardware debugging tool on a microcontroller.

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> gpio 4 6 t
P4.6 -&gt; 1

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> gpio 4 6
P4.6 = 1 (output)

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> adc temp
Atemp = 1847 (0x737)

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> adc bat
Abat = 2048 (0x800)
</pre>
</td></tr></table>

---

### :bar_chart: Memory Introspection

Real numbers from linker symbols and the stack pointer — not placeholders.

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> free
<span style="color:#c4a000">--- Compile-time ---</span>
<b>SRAM</b>   2048 total
  .data+.bss   1058
  reservd        990
<b>FRAM</b>  65535 total
  code        42508
  const/data   5620
  unallocd    17407
<span style="color:#4e9a06">--- Runtime ---</span>
<b>SRAM</b>
  stack now      80
  free now    <b>  910</b>
<b>FRAM</b>
  config rgn   1024 allocated
  init table     70 (1/8 entries)
<span style="color:#06989a">--- Processes (1/8) ---</span>
 pid  name        sram  fram  state
   0  Shell          0     0  running
</pre>
</td></tr></table>

---

### :battery: Power Management

Enter ultra-low-power modes from the shell. See what will wake you up.

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> sleep lpm3
Idle: LPM3

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> wake
Wake sources:
  Timer A0 (sys clock)  [on ]  wakes LPM0-3
  Timer A1 (htimer)     [off]  wakes LPM0-3
  UART RX  (eUSCI_A0)   [on ]  wakes LPM0
  Watchdog (interval)   [off]  wakes LPM0-3
  GPIO IRQ              [off]  wakes LPM0-4

Note: LPM4 disables all clocks.
  Only GPIO IRQ can wake from LPM4.
</pre>
</td></tr></table>

---

### :open_file_folder: Virtual Filesystem

Unix-like VFS for system introspection and device control.

<table><tr><td>
<pre>
<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> ls /
  sys/
  dev/

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> ls /dev
  led0
  led1

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> read /sys/uptime
42

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> write /dev/led0 1

<span style="color:#4e9a06"><b>tikuOS&gt;</b></span> cat /sys/mem/sram
2048
</pre>
</td></tr></table>

---

### Build Options

| Flag | Effect |
|------|--------|
| `TIKU_SHELL_ENABLE=1` | Enable interactive shell (UART) |
| `TIKU_INIT_ENABLE=1` | Enable FRAM-backed init system (implies shell) |
| `TIKU_SHELL_COLOR=1` | Enable ANSI color output (banner, prompt, help, free) |
| `UART_BAUD=115200` | Set UART baud rate (default 9600) |
| `MCU=msp430fr5969` | Target MCU (fr5969, fr5994, fr2433) |

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
