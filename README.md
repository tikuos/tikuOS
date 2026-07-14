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
  <a href="#supported-boards"><img src="https://img.shields.io/badge/MCU-MSP430%20%7C%20Apollo%20%7C%20RP2350%20%7C%20nRF54L-red?style=flat-square" alt="MCU"></a>
  <a href="#license"><img src="https://img.shields.io/badge/license-Apache%202.0-green?style=flat-square" alt="License"></a>
  <a href="#interactive-shell"><img src="https://img.shields.io/badge/shell-58%20commands-orange?style=flat-square" alt="Shell"></a>
  <a href="#networking"><img src="https://img.shields.io/badge/net-Wi--Fi%20%7C%20TLS%20%7C%20MQTT-brightgreen?style=flat-square" alt="Networking"></a>
  <a href="http://tiku-os.org"><img src="https://img.shields.io/badge/web-tiku--os.org-purple?style=flat-square" alt="Website"></a>
</p>

<p align="center">
  TikuOS is an operating system for <strong>microwatt computers</strong>: ubiquitous devices that run for years on a coin cell or indefinitely on harvested energy. It is the first OS designed for sub-milliwatt communication primitives, including backscatter and tunnel diode based beyond-backscatter transceivers, with IP networking and machine intelligence as integral parts of the operating system.
</p>

<p align="center"><sub>Event-driven protothreads · static allocation, no heap · one source tree, three CPU architectures.</sub></p>

---

## Supported Boards

| Family | Board · core | RAM | NVM | Status |
|--------|--------------|-----|-----|--------|
| **MSP430**<br><sub>16-bit · FRAM</sub> | FR5994 LaunchPad | 8 KB | 256 KB | :green_circle: |
| | FR6989 LaunchPad | 2 KB | 128 KB | :green_circle: |
| **Ambiq Apollo**<br><sub>32-bit · Cortex-M</sub> | Apollo510 EVB · M55 96/250 MHz | 512 KB | 4 MB MRAM | :green_circle: |
| | Apollo4 Lite EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM | :green_circle: |
| | Apollo4 Plus EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM | :green_circle: |
| | Apollo510 Blue EVB · M55 + EM9305 | 512 KB | 4 MB MRAM | :green_circle: |
| **Raspberry Pi**<br><sub>32-bit · RP2350 · Cortex-M33 @ 150 MHz</sub> | Pico 2 / Pico 2 W | 520 KB | 4 MB flash | :green_circle: |
| **Nordic**<br><sub>32-bit · Cortex-M33</sub> | nRF54L15 DK | 256 KB | 1.5 MB RRAM | :green_circle: |
| | nRF54LM20-DK · 128 MHz + Axon NPU | 512 KB | 2 MB RRAM | :green_circle: |

---

## Quick Start

```bash
# --- MSP430 (msp430-gcc + mspdebug) ----------------------------------------
make flash MCU=msp430fr5994 MEMORY_MODEL=large TIKU_SHELL_ENABLE=1

# --- Ambiq Apollo (arm-none-eabi-gcc + SEGGER J-Link) ----------------------
make flash MCU=apollo510 TIKU_SHELL_ENABLE=1   # also apollo4l / apollo4p / apollo510b

# --- Raspberry Pi Pico 2 / 2 W (arm-none-eabi-gcc + picotool) ---------------
make MCU=rp2350                                # builds main.elf, main.bin, main.uf2
make flash MCU=rp2350                          # picotool, or copy the UF2 to RPI-RP2

# --- Nordic nRF54L (arm-none-eabi-gcc + nrfutil / SEGGER J-Link) ------------
make flash MCU=nrf54lm20b TIKU_SHELL_ENABLE=1  # also nrf54lm20a / nrf54l15
```

The kernel, shell, VFS, BASIC, and networking are architecture-neutral — the
same source tree targets all four families through a device/board header
abstraction.

---

## :computer: Interactive Shell

A full interactive shell over UART, USB-CDC, or Telnet. Manage processes, drive
GPIO and sensors, join Wi-Fi, run BASIC, and inspect memory — **all without
recompiling**.

```
  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/  v0.05
  Simple. Ubiquitous. Intelligence, Everywhere.

  Apollo510  |  SRAM 512KB  MRAM 4MB
  Type 'help' for commands.

tikuOS> help
 System     help  info  free  mem  reboot  history  clear  echo  syslog
 Processes  ps  start  kill  resume  queue  timer  every  once  jobs  on  rules
 Files      ls  tree  cd  read  write  watch  name  alias  toggle  fs  df
 Network    wifi  ip  slip  ping  dns  ntp  mqtt  bt
 Language   basic
 Hardware   gpio  adc  i2c  lcd  irq  freq  trng  nvmprobe  mrambench
 Power      sleep  wake
 Boot       init
 (58 commands total — run 'help' on the device for the full list)
```

> :bulb: The same shell and VFS are byte-identical on every port. Opt-in extras
> (off by default, enabled via `EXTRA_CFLAGS`): `if`, `delay`, `repeat`,
> `peek`, `poke`, `i2c`. See `kernel/shell/tiku_shell_config.h`.

---

## :satellite: Networking

TikuOS 0.05 speaks IP. The stack is event-driven and statically allocated,
sized for these parts, and reachable from the shell, from BASIC, and from C.

- **Link / transport** — IPv4, TCP, UDP, ARP, SLIP over the console UART, and a
  clean-room Wi-Fi driver for the **CYW43439** (Pico 2 W): gSPI, WHD/SDPCM,
  firmware upload, scan/join, proven over the air.
- **Security** — **TLS 1.3 and 1.2** with RSA, P-256 and P-384 ECDHE, and
  AES-256-GCM; PSK *and* X.509 certificate-chain verification against a
  built-in CA trust store.
- **Applications** — an **MQTT 3.1.1** client (QoS 0/1, optional TLS), **HTTPS**
  GET, DNS, an NTP client, ping, remote syslog, and a **Telnet** server that
  hands the shell over TCP.

```
tikuOS> wifi join <ssid> <pass>
tikuOS> ntp pool.ntp.org
tikuOS> mqtt connect test.mosquitto.org 8883 tls
tikuOS> ping tiku-os.org
```

> The IP stack, Wi-Fi driver, TLS engine, and MQTT client live in the companion
> **tikukits** library (`TIKU_KIT_NET_ENABLE=1`); the shell and BASIC commands
> that drive them ship in the core kernel.

---

## :abc: Tiku BASIC

A complete on-device BASIC interpreter — write a program over the shell, `RUN`
it, `SAVE` it to non-volatile storage, and `LOAD` it back after a power cycle.
No host toolchain required.

```basic
10 PRINT "reading temperature..."
20 T = VAL(HTTPGET$("http://api.example.com/temp"))
30 IF T > 30 THEN MQTTPUB "alerts/hot", STR$(T)
40 EVERY 60 GOSUB 20
```

Numeric and string variables, arrays, `DEF FN` user functions, `ON ERROR`,
reactive `EVERY` / `ON … CHANGE` statements, an HTML-to-text renderer, plus
builtins for HTTP(S), MQTT, JSON, SHA-256/HMAC/Base64, and a `NOW`/`SETTIME`
clock. Programs persist to NVM.

```bash
make ... TIKU_SHELL_BASIC_ENABLE=1 MEMORY_MODEL=large
```

---

## :wrench: Hardware Debugging from the Shell

Toggle GPIO pins, read ADC channels — a live hardware debugging tool on a
microcontroller.

```
tikuOS> gpio 4 6 t
P4.6 -> 1

tikuOS> gpio 4 6
P4.6 = 1 (output)

tikuOS> adc temp
Atemp = 1847 (0x737)
```

---

## :floppy_disk: Durable Storage

Storage is a hierarchy, not a flat `.persistent` section — configuration, BASIC
programs, and files survive reboots and power loss:

- **Memory tiers** — one API places allocations in SRAM, HIFRAM, or NVM by
  policy, with a bump allocator per tier.
- **NVM region allocator** — carves on-chip MRAM/flash into a tier extent, a
  file-store extent, and a reserved tail.
- **Persist cells** — self-validating, magic-gated non-volatile variables with
  a torn-write-safe commit.
- **`/data` file store** — a small durable filesystem, exposed at `/data` and
  used by BASIC `SAVE`/`LOAD` and the shell's `df` / `ls` / `write`.

Boot behavior itself is FRAM/NVM-backed: change the startup sequence over the
shell and reboot — no recompile, no reflash.

```
tikuOS> init add 10 net    start net
tikuOS> init add 20 mqtt   start mqtt
tikuOS> init list
 10  net          [on ]  start net
 20  mqtt         [on ]  start mqtt
```

---

## :open_file_folder: Virtual Filesystem

A unified namespace for the entire system — peripherals, OS state, config, and
processes are all paths. The same `read`/`write` interface works for LEDs,
sensors, timers, processes, and everything else. The namespace is also an event
bus: `watch` monitors a node and reports each change, `changed` blocks until the
next one, and `on` / `rules` register reactive rules that fire on a change.

```
/
├── sys/           version, uptime, device/, mem/, cpu/, power/, timer/,
│                  clock/, watchdog/, htimer/, boot/, sched/, last_reset
├── dev/           led0..N, console, null, zero, gpio/, gpio_dir/, uart/,
│                  adc/, i2c/, spi/
├── data/          durable file store (df, ls, read, write)
└── proc/          per-process state: name, state, pid, sram/fram_used,
                   uptime, wake_count, events
```

```
tikuOS> cat /sys/device/mcu
Apollo510

tikuOS> cat /sys/last_reset
watchdog

tikuOS> write /dev/led0 1

tikuOS> changed /dev/gpio/1/3      # block until P1.3 changes
```

---

## :zap: Reliability

- **Check-in hang watchdog** — a process that stops making progress is named as
  the culprit, warm-reset, and quarantined on the recovery boot, so one wedged
  service can't take down the console.
- **Process supervision** — failed processes restart under a configurable
  policy, with stale events dropped across the restart.
- **Hardware watchdog + reset-reason** — real register-level implementations on
  the platforms that have them, surfaced at `/sys/last_reset`.
- **Preemptive worker threads** (Cortex-M) run compute under a cycle/energy
  budget and offload multi-second TLS crypto without freezing the console.

---

## Build Options

| Flag | Effect |
|------|--------|
| `MCU=…` | Target: `msp430fr5994`, `msp430fr6989`, `apollo510`, `apollo4l`, `apollo4p`, `apollo510b`, `rp2350`, `nrf54l15`, `nrf54lm20a`, `nrf54lm20b` |
| `TIKU_SHELL_ENABLE=1` | Interactive shell (UART / USB-CDC / Telnet) |
| `TIKU_SHELL_BASIC_ENABLE=1` | Tiku BASIC interpreter (needs `MEMORY_MODEL=large`) |
| `TIKU_INIT_ENABLE=1` | FRAM/NVM-backed boot sequence (implies shell) |
| `TIKU_KIT_NET_ENABLE=1` | IP / Wi-Fi / TLS / MQTT (companion tikukits) |
| `MEMORY_MODEL=large` | 20-bit pointers + HIFRAM placement (MSP430 large working sets) |
| `UART_BAUD=…` | UART baud (default 9600 on MSP430, 115200 on Cortex-M) |

---

## Testing, benchmarks, and releases

TikuBench is the canonical test runner for every supported board/profile:

```bash
cd TikuBench
python -m tikubench run kernel --board <board> --flash
python -m tikubench bench report
python -m tikubench coverage
python -m tikubench release
```

The [generated kernel matrix](TikuBench/docs/generated/KERNEL_MATRIX.md) is the
current test inventory. The [runner](TikuBench/docs/RUNNER.md),
[benchmark](TikuBench/docs/BENCHMARKS.md),
[fixture](TikuBench/docs/HARDWARE_FIXTURES.md), and
[release](TikuBench/docs/RELEASE.md) documents define evidence and freshness
requirements; selected diagnostics never replace a full verdict.

---

## Minimal Example

```c
#include "tiku.h"

TIKU_PROCESS(blink_process, "Blink");
static struct tiku_timer timer;

TIKU_PROCESS_THREAD(blink_process, ev, data)
{
    TIKU_PROCESS_BEGIN();
    tiku_led_init(0);
    tiku_timer_set_event(&timer, TIKU_CLOCK_SECOND);
    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);
        tiku_led_toggle(0);
        tiku_timer_reset(&timer);
    }
    TIKU_PROCESS_END();
}

TIKU_AUTOSTART_PROCESSES(&blink_process);
```

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
