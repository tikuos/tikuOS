# TikuOS

TikuOS is an operating system for microwatt computers: devices that run for
years on a coin cell or indefinitely on harvested energy.  It is built for
sub-milliwatt communication, including backscatter and tunnel-diode
transceivers, with IP networking and on-device intelligence as parts of the
operating system rather than add-ons.  Event-driven protothreads, static
allocation, no heap; one source tree for 16-bit MSP430, Cortex-M and RISC-V.

Most changes to this tree are made by coding agents working with a
person.  [Working on TikuOS](#working-on-tikuos) says how to start one and
what an agent reads first.

## Supported boards

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
| **Renesas RA8**<br><sub>32-bit · Cortex-M85</sub> | EK-RA8P1 · M85 240/480/1000 MHz + Ethos-U55 | 1664 KB | 1 MB MRAM | :green_circle: |
| **STMicro STM32N6**<br><sub>32-bit · Cortex-M55</sub> | Nucleo-144 N657X0-Q · up to 600 MHz | 3840 KB | 64 MB NOR | :yellow_circle: |
| **Espressif ESP32-C61**<br><sub>32-bit · RISC-V @ 160 MHz</sub> | ESP32-C61-DevKitC · Wi-Fi 6 + BLE 5 | 320 KB + 2 MB PSRAM | 8 MB flash | :green_circle: |

:green_circle: verified by the TikuBench matrix · :yellow_circle: brought up,
not yet in that matrix.  The RA8P1 also drives 64 MB of SDRAM and 64 MB of
Octo-SPI NOR; the STM32N6 keeps its durable store on external NOR; the
ESP32-C61 runs its larger code from flash and keeps big buffers in PSRAM.

## Getting the source

```bash
git clone --recurse-submodules https://github.com/tikuos/tikuOS.git
cd tikuOS
```

`drivers/` is a submodule ([tikuos/drivers](https://github.com/tikuos/drivers));
without it the build falls back to an empty driver table.  The companion
checkouts live inside this tree but are versioned separately and are
gitignored here: `TikuBench/` (tests and the board runner), `tikukits/`
(IP stack, Wi-Fi, TLS, MQTT, maths, sensors), `applications/` (the desktop
and board GUI), `tikuConsole/`, and the private `applications/tracker/`.  A
public clone never fails on a repository the cloner cannot read.

## Build and flash

```bash
# MSP430 (msp430-gcc + mspdebug)
make flash MCU=msp430fr5994 MEMORY_MODEL=large TIKU_SHELL_ENABLE=1
# Ambiq Apollo (arm-none-eabi-gcc + SEGGER J-Link); also apollo4l apollo4p apollo510b
make flash MCU=apollo510 TIKU_SHELL_ENABLE=1
# Raspberry Pi Pico 2 / 2 W (arm-none-eabi-gcc + picotool, or copy main.uf2)
make flash MCU=rp2350
# Nordic nRF54L (arm-none-eabi-gcc + nrfutil / J-Link); also nrf54lm20a nrf54l15
make flash MCU=nrf54lm20b TIKU_SHELL_ENABLE=1
# Renesas EK-RA8P1 (arm-none-eabi-gcc + on-board J-Link); boots at 240 MHz
make flash MCU=ra8p1 TIKU_SHELL_ENABLE=1
# ST Nucleo-N657X0-Q (arm-none-eabi-gcc + STM32CubeProgrammer)
make flash MCU=stm32n6 TIKU_SHELL_ENABLE=1
# Espressif ESP32-C61-DevKitC (riscv32 newlib gcc + esptool); radios: sh drivers/wifi/esp/fetch.sh
make flash MCU=esp32c61 TIKU_SHELL_ENABLE=1
```

| Flag | Effect |
|------|--------|
| `MCU=…` | `msp430fr5994` `msp430fr6989` `apollo510` `apollo4l` `apollo4p` `apollo510b` `rp2350` `nrf54l15` `nrf54lm20a` `nrf54lm20b` `ra8p1` `stm32n6` `esp32c61` |
| `TIKU_SHELL_ENABLE=1` | the shell over UART, USB-CDC or Telnet |
| `TIKU_SHELL_BASIC_ENABLE=1` | Tiku BASIC (with `MEMORY_MODEL=large` on MSP430) |
| `TIKU_INIT_ENABLE=1` | NVM-backed boot sequence, edited from the shell |
| `TIKU_KIT_NET_ENABLE=1` | IP, Wi-Fi, TLS and MQTT from tikukits |
| `TIKU_ESP32C61_XIP_CODE=1` `TIKU_ESP32C61_PSRAM_DATA=1` | ESP32-C61 code from flash, buffers in PSRAM |
| `UART_BAUD=…` | 9600 on MSP430, 115200 elsewhere by default |
| `EXTRA_CFLAGS=-DNRF_SKIP_GLITCHDETECTOR_DISABLE` | nRF54L: keep the voltage glitch detector on (startup disables it to save power) |

## What is in the tree

| Directory | Holds | Read |
|-----------|-------|------|
| `kernel/` | processes and scheduler, timers, memory tiers and durable placement, the VFS, the file store, threads, the driver registry | [`kernel/vfs/AGENTS.md`](kernel/vfs/AGENTS.md), [`kernel/memory/AGENTS.md`](kernel/memory/AGENTS.md) |
| `services/` | console framing, message links over the console, IP and BLE, USB device classes, the init table | [`services/console/AGENTS.md`](services/console/AGENTS.md), [`services/link/AGENTS.md`](services/link/AGENTS.md) |
| `shell/` | the interactive shell and its commands | [`shell/AGENTS.md`](shell/AGENTS.md) |
| `basic/` | the Tiku BASIC interpreter and native modules | [`basic/AGENTS.md`](basic/AGENTS.md) |
| `boot/`, `main.c` | startup and the choice of optional components | |
| `hal/`, `interfaces/`, `arch/` | hardware contracts and the per-family ports | [`drivers.md`](drivers.md) |
| `drivers/` | peripheral and radio drivers (submodule) | [`drivers/wifi/esp/README.md`](drivers/wifi/esp/README.md) |
| `tools/` | lint and host-side tools | [`tools/README.md`](tools/README.md) |

The shell is the system's console: every peripheral, process and setting is
a path under `/sys`, `/dev`, `/proc` and `/data`, read and written with the
same commands, watched for change, and driven by rules.  Networking (IPv4,
TCP, UDP, SLIP, Wi-Fi, TLS 1.2/1.3, MQTT, HTTPS, DNS, NTP, Telnet) and the
BLE host stack are reachable from the shell, from BASIC and from C.  Storage
is tiered (SRAM, FRAM, NVM) with self-validating persist cells and a small
durable file store at `/data`.  A check-in watchdog names, resets and
quarantines a process that stops making progress.

## Testing

TikuBench runs every suite against a board or on the host:

```bash
cd TikuBench
python -m tikubench list --board nrf54l15
python -m tikubench run kernel --board nrf54l15 --flash
python -m tikubench all --board nrf54l15 --flash
make -C tests/host run          # host suites, no board
```

`make lint` at the root runs the comment-style, durable-placement and
kernel-dependency checks.  [`TikuBench/docs/generated/KERNEL_MATRIX.md`](TikuBench/docs/generated/KERNEL_MATRIX.md)
is the current test inventory.

## Working on TikuOS

The tree is developed with coding agents (Claude Code, Codex and the
like) run at its root, one task per session and one capability per
commit.  To start:

```bash
git clone --recurse-submodules https://github.com/tikuos/tikuOS.git
cd tikuOS
sh hygiene/install_hooks.sh     # commit-message and author checks, per repository
claude                          # or: codex
```

Ask for a capability, not a step: "add X, prove it with TikuBench on the
nRF54L15, show me the commit message".  The agent builds, runs the proof,
and presents the commit subject and body before committing; pushing is a
separate request.  One agent at a time on the connected boards.

**Agents start here.**  Read [`AGENTS.md`](AGENTS.md) before changing
anything: it holds the source-ownership rule, the build, test and lint
commands, the comment and commit contracts and what is never committed.
Then read the document in the table above for the area you are about to
touch, and [`comment-style.md`](comment-style.md) before writing a comment.
Each `AGENTS.md` is written for an agent: facts, rules and proof, no
introduction.  Local notes that are not part of the OS (`kintsugi/`,
`experiment/`) are gitignored and never committed.

## Minimal example

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

## License

Apache License 2.0; see [LICENSE](LICENSE).
[tiku-os.org](http://tiku-os.org) · ambuj@tiku-os.org
