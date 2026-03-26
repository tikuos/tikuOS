# TikuOS

**Simple. Ubiquitous. Intelligence, Everywhere.**

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
