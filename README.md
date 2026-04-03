# TikuOS

**Simple. Ubiquitous. Intelligence, Everywhere.**

TikuOS is an operating system for microwatt computers: tiny, ubiquitous devices that run for years on a coin cell or indefinitely on harvested energy. It is the first OS designed for sub-milliwatt communication primitives, including backscatter and tunnel diode based beyond-backscatter transceivers, with IP networking and machine intelligence as integral parts of the operating system.

---

## Supported Boards

| Board | MCU | RAM | FRAM |
|-------|-----|-----|------|
| MSP-EXP430FR5969 LaunchPad | MSP430FR5969 | 2 KB | 64 KB |

## Architecture

- MSP430 with FRAM support variant

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

## Interactive Shell

TikuOS includes a full interactive shell over UART or Telnet. Control GPIO pins, read sensors, manage processes, configure boot sequences, and inspect memory — all without recompiling.

```
  ___ _ _         ___  ___
 |_ _|_) |_ _  _/ _ \/ __|
  | || | / / || | (_) \__ \
  |_||_|_\_\\_,_|\___/|___/  v0.01
  Simple. Ubiquitous. Intelligence, Everywhere.

  MSP430FR5969  |  SRAM 2048B  FRAM 64KB
  Type 'help' for commands.

tikuOS> help
 --- System ---
  help       Show available commands
  info       Device, CPU, uptime, clock
  free       Memory usage (SRAM/FRAM)
  reboot     System reset
 --- Processes ---
  ps         List active processes
  start      Start/resume by name
  kill       Stop a process (by pid)
  resume     Resume a stopped process
 --- Filesystem ---
  ls         List directory
  cd         Change directory
  read       Read a VFS node
  write      Write a VFS node
 --- Hardware ---
  gpio       Read/write GPIO pins
  adc        Read analog channel
 --- Power ---
  sleep      Set low-power idle mode
  wake       Show active wake sources
 --- Boot ---
  init       Manage FRAM boot entries
```

### Configurable Boot Sequence (FRAM-backed)

Every other RTOS: change boot behavior → recompile → reflash.
TikuOS: change boot behavior → edit over shell → reboot.

```
tikuOS> init add 05 network start net
tikuOS> init add 10 mqtt    start mqtt
tikuOS> init add 20 leds    write /dev/led0 1
tikuOS> init list
 05  network      [on ]  start net
 10  mqtt         [on ]  start mqtt
 20  leds         [on ]  write /dev/led0 1
```

Boot entries are stored in FRAM — they survive power cycles without flash erase cycles. Same firmware, different boot sequences per device.

### Hardware Debugging from the Shell

```
tikuOS> gpio 4 6 t
P4.6 -> 1
tikuOS> adc temp
Atemp = 1847 (0x737)
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
tikuOS> sleep lpm3
Idle: LPM3
```

Build with the shell:

```bash
make TIKU_SHELL_ENABLE=1 MCU=msp430fr5969       # shell only
make TIKU_INIT_ENABLE=1 MCU=msp430fr5969         # shell + init system
make monitor                                      # connect over UART
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

## Contact

- Website: [http://tiku-os.org](http://tiku-os.org)
- Email: <ambuj@tiku-os.org>
