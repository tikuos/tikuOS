# TikuOS
TikuOS is an operating system for ubiquitous computers, designed around the microwatt computer that runs for years on a coin cell or indefinitely on harvested energy. We designed it from scratch to interact with the physical world, work alongside AI agents that both use and program it, and sustain long-term operation. The software stack supports sub-milliwatt wireless communication, IP networking, and on-device intelligence, and even runs on feeble energy harvested from the environment for devices with tiny or no batteries.

## 🚀 Getting started

**1️⃣ Clone the tree**

```bash
git clone --recurse-submodules https://github.com/tikuos/tikuOS.git
cd tikuOS
```

**2️⃣ Start a coding agent** (Claude Code, Codex or similar) at the root of
the tree and ask for what you want:

```bash
claude      # or: codex
> Write a process that blinks LED 0 every second on the nRF54L15 and flash it
> Add a shell command that prints the battery voltage and test it on the FR5994
```

🤖 The agent reads `AGENTS.md`, builds, flashes and tests, and shows you the
commit before making it.

## 🔌 Supported boards

### ⚡ Microwatt computers

| Family | Board · core | RAM | NVM |
|--------|--------------|-----|-----|
| **MSP430**<br><sub>16-bit · FRAM</sub> | FR5994 LaunchPad | 8 KB | 256 KB |
| | FR6989 LaunchPad | 2 KB | 128 KB |
| **Ambiq Apollo**<br><sub>32-bit · Cortex-M</sub> | Apollo510 EVB · M55 96/250 MHz | 512 KB | 4 MB MRAM |
| | Apollo4 Lite EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM |
| | Apollo4 Plus EVB · M4F 96/192 MHz | 384 KB | 2 MB MRAM |
| | Apollo510 Blue EVB · M55 + EM9305 | 512 KB | 4 MB MRAM |
| **Nordic**<br><sub>32-bit · Cortex-M33</sub> | nRF54L15 DK | 256 KB | 1.5 MB RRAM |
| | nRF54LM20-DK · 128 MHz + Axon NPU | 512 KB | 2 MB RRAM |

### 🧩 Others

| Family | Board · core | RAM | NVM |
|--------|--------------|-----|-----|
| **Raspberry Pi**<br><sub>32-bit · RP2350 · Cortex-M33 @ 150 MHz</sub> | Pico 2 / Pico 2 W | 520 KB | 4 MB flash |
| **Renesas RA8**<br><sub>32-bit · Cortex-M85</sub> | EK-RA8P1 · M85 240/480/1000 MHz + Ethos-U55 | 1664 KB | 1 MB MRAM |
| **STMicro STM32N6**<br><sub>32-bit · Cortex-M55</sub> | Nucleo-144 N657X0-Q · up to 600 MHz | 3840 KB | 64 MB NOR |
| **Espressif**<br><sub>32-bit · RISC-V</sub> | ESP32-C61-DevKitC · 160 MHz · Wi-Fi 6 + BLE 5 | 320 KB + 2 MB PSRAM | 8 MB flash |
| | ESP32-C5-DevKitC-1 · 240 MHz · dual-band Wi-Fi 6 + BLE 5 + 802.15.4 | 384 KB | 4 MB flash |
| | Olimex ESP32-P4-PC · 360 MHz | 768 KB + 32 MB PSRAM | 16 MB flash |

## 🐚 Interacting with TikuOS boards

Every part of the operating system and the hardware is represented as a
file under a path such as `/sys`, `/dev`, `/proc` or `/data`, and can be
read and changed with shell commands, so agents and people interact
with a board the same way:

```
tikuOS:/> ls /sys
tikuOS:/> read /sys/cpu/freq
tikuOS:/> write /dev/led0 1
tikuOS:/> every 1 toggle /dev/led0
```

TikuOS also has a BASIC interpreter. With agents writing the C, it is the
simplest way for a person to program a device by hand, a few lines at the
board rather than a C program. Programs save to `/data` and survive a
power cut:

```
tikuOS:/> basic
ok> 10 FOR I = 1 TO 3
ok> 20 PRINT "hello "; I
ok> 30 NEXT I
ok> RUN
```

For those who prefer to write C themselves, the TikuOS API is one header,
`tiku.h`, and the kits (networking, TLS, MQTT, sensors, maths) provide the
functionality for designing complex systems that interact with the
physical world. The example below demonstrates a minimal TikuOS program.

## 💡 Minimal example

A process that blinks LED 0 once a second, in C:

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

The same blink in BASIC, typed at the board or kept as a `.bas` file in
`/data` and run with `basic run /data/blink.bas`:

```
10 LED 0, 2
20 DELAY 1000
30 GOTO 10
```

## 📄 License

Apache License 2.0; see [LICENSE](LICENSE).
[tiku-os.org](http://tiku-os.org) · ambuj@tiku-os.org
