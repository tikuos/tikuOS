# Supported Platforms

TikuOS currently targets the **TI MSP430** family of ultra-low-power FRAM microcontrollers. The architecture is designed around a two-level abstraction -- **devices** (silicon) and **boards** (PCB) -- so that adding new variants requires minimal changes.

---

## Supported Boards

| Board | MCU | RAM | FRAM | Max Clock | LFXT | HFXT | GPIO Ports |
|-------|-----|-----|------|-----------|------|------|------------|
| MSP-EXP430FR2433 LaunchPad | MSP430FR2433 | 4 KB | 16 KB | 16 MHz | No | No | P1-P3 |
| MSP-EXP430FR5969 LaunchPad | MSP430FR5969 | 2 KB | 64 KB | 8 MHz | Yes | Yes | P1-P4, PJ |
| MSP-EXP430FR5994 LaunchPad | MSP430FR5994 | 8 KB | 256 KB | 16 MHz | Yes | Yes | P1-P8, PJ |

---

## How Platform Selection Works

Platform selection has two layers:

### 1. Build-Time MCU Selection (Makefile)

Pass the `MCU` variable when invoking Make:

```bash
make MCU=msp430fr5969
make MCU=msp430fr5994
make MCU=msp430fr2433    # default if omitted
```

The Makefile derives the device define automatically:

```
MCU=msp430fr5969  ->  -DTIKU_DEVICE_MSP430FR5969=1
MCU=msp430fr5994  ->  -DTIKU_DEVICE_MSP430FR5994=1
MCU=msp430fr2433  ->  -DTIKU_DEVICE_MSP430FR2433=1
```

### 2. Header Routing (`tiku_device_select.h`)

The file `arch/msp430/tiku_device_select.h` checks which `TIKU_DEVICE_*` macro is defined and includes the matching device and board headers:

```c
#if defined(TIKU_DEVICE_MSP430FR5969)
#include <arch/msp430/devices/tiku_device_fr5969.h>
#include <arch/msp430/boards/tiku_board_fr5969_launchpad.h>

#elif defined(TIKU_DEVICE_MSP430FR5994)
#include <arch/msp430/devices/tiku_device_fr5994.h>
#include <arch/msp430/boards/tiku_board_fr5994_launchpad.h>

#elif defined(TIKU_DEVICE_MSP430FR2433)
#include <arch/msp430/devices/tiku_device_fr2433.h>
#include <arch/msp430/boards/tiku_board_fr2433_launchpad.h>
#endif
```

If no device is defined (e.g. compiling without the Makefile), `tiku.h` defaults to `TIKU_DEVICE_MSP430FR2433`.

---

## Device vs. Board Headers

TikuOS separates platform definitions into two header types:

### Device Headers (`arch/msp430/devices/`)

Define **silicon-level constants** that are fixed for a given MCU, regardless of which PCB it sits on:

- GPIO port availability (`TIKU_DEVICE_HAS_PORTx`)
- Crystal oscillator support (`TIKU_DEVICE_HAS_LFXT`, `TIKU_DEVICE_HAS_HFXT`)
- Crystal pin routing registers
- Clock system type and capabilities
- Memory sizes (FRAM, SRAM)
- Maximum stable clock frequency

### Board Headers (`arch/msp430/boards/`)

Define **PCB-level pin assignments** specific to a particular development board:

- LED pin init/on/off/toggle macros (`TIKU_BOARD_LED1_*`, `TIKU_BOARD_LED2_*`)
- Button pin init/read macros (`TIKU_BOARD_BTN1_*`, `TIKU_BOARD_BTN2_*`)
- UART pin configuration and baud rate settings
- Board name string (`TIKU_BOARD_NAME`)

All arch-level `.c` files reference only the generic `TIKU_DEVICE_*` and `TIKU_BOARD_*` macros, so they compile unchanged across all supported variants.

---

## Adding a New MSP430 Variant

To add support for a new MSP430 device and board:

### Step 1: Create a Device Header

Create `arch/msp430/devices/tiku_device_<variant>.h` defining the silicon constants. Use an existing header (e.g. `tiku_device_fr5969.h`) as a template. At minimum, define:

- `TIKU_DEVICE_NAME`
- `TIKU_DEVICE_HAS_PORTx` for all ports
- `TIKU_DEVICE_HAS_LFXT` / `TIKU_DEVICE_HAS_HFXT`
- `TIKU_DEVICE_CS_HAS_KEY`
- `TIKU_DEVICE_MAX_STABLE_MHZ`
- `TIKU_DEVICE_FRAM_SIZE` / `TIKU_DEVICE_RAM_SIZE`
- Crystal pin routing macros (if LFXT/HFXT are available)

### Step 2: Create a Board Header

Create `arch/msp430/boards/tiku_board_<variant>_<boardname>.h` defining the PCB-level pin assignments. At minimum, define:

- `TIKU_BOARD_NAME`
- `TIKU_BOARD_LED1_*` macros (init, on, off, toggle)
- `TIKU_BOARD_LED2_*` macros
- `TIKU_BOARD_BTN1_*` / `TIKU_BOARD_BTN2_*` macros
- `TIKU_BOARD_UART_*` macros (pin init, clock select, baud config)

### Step 3: Add an `#elif` Clause

In `arch/msp430/tiku_device_select.h`, add:

```c
#elif defined(TIKU_DEVICE_MSP430FRXXXX)
#include <arch/msp430/devices/tiku_device_frxxxx.h>
#include <arch/msp430/boards/tiku_board_frxxxx_boardname.h>
```

### Step 4: Update the Default Guard (optional)

If you want the new device recognized in the fallback guard in `tiku.h`, add it to the `#if !defined(...)` chain:

```c
#if !defined(TIKU_DEVICE_MSP430FR5969) && \
    !defined(TIKU_DEVICE_MSP430FR5994) && \
    !defined(TIKU_DEVICE_MSP430FR2433) && \
    !defined(TIKU_DEVICE_MSP430FRXXXX)
#define TIKU_DEVICE_MSP430FR2433 1
#endif
```

### Step 5: Build and Test

```bash
make MCU=msp430frxxxx
make flash MCU=msp430frxxxx
```

No other source files need modification -- the generic `TIKU_DEVICE_*` / `TIKU_BOARD_*` macros ensure all arch `.c` files compile for the new target automatically.

---

## Key Differences Between Supported Devices

### MSP430FR2433

- Smallest variant: 4 KB RAM, 16 KB FRAM
- No external crystal support (LFXT/HFXT)
- Only 3 GPIO ports (P1-P3), no Port J
- Clock system uses FLL with `DCORSEL` (no `CSKEY` password)
- Combined MCLK/SMCLK source select (`SELMS`)

### MSP430FR5969

- Mid-range: 2 KB RAM, 64 KB FRAM
- Full crystal support (LFXT on PJ.4/5, HFXT on PJ.2/3)
- 4 GPIO ports (P1-P4) plus Port J
- CS_A module with `CSKEY` password protection
- Maximum stable frequency: 8 MHz

### MSP430FR5994

- Largest variant: 8 KB RAM, 256 KB FRAM
- Full crystal support (LFXT on PJ.4/5, HFXT on PJ.6/7)
- 8 GPIO ports (P1-P8) plus Port J
- CS_A module with `CSKEY` password protection
- Maximum stable frequency: 16 MHz
