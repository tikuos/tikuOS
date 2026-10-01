# tikuOS on the ESP32-C61-DevKitC — Mac Setup Guide

This guide gets tikuOS building and running on Espressif's **ESP32-C61-DevKitC**
(the chip is the **ESP32-C61**, a RISC-V processor). No debugger probe and no
drivers: the board's own USB port installs the program, and its second USB port
is the console.

> 🧭 **New here?** Do [Part 0 of the README](README.md) first (Command Line
> Tools + Homebrew). This guide assumes it's done.

**Time needed:** about 15 minutes.

---

## What you need

- An **ESP32-C61-DevKitC** board.
- **Two USB-C data cables** (not charge-only). The board has two USB-C ports,
  and tikuOS uses both:
  - **USB** — the chip's own USB port. tikuOS is loaded through this one.
  - **UART** — a USB-to-serial bridge chip (a CP2102N). The tikuOS console is
    on this one.

Plug both into your Mac, then check that both ports appeared:

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial*
```

You should see two names, for example `/dev/cu.usbmodem1201` (the **USB** port)
and `/dev/cu.usbserial-110` (the **UART** port). The numbers differ from Mac to
Mac.

---

## Step 1 — Install the RISC-V compiler

The ESP32-C61 is a RISC-V chip, so the Arm compiler from the other guides won't
do. We use **Espressif's RISC-V GCC**, which comes with the small C library
tikuOS links against. It isn't in Homebrew, so download it directly:

```bash
V=esp-16.1.0_20260609
ARCH=$(uname -m | sed 's/arm64/aarch64/')     # aarch64 on Apple Silicon
mkdir -p ~/.espressif/tools/riscv32-esp-elf/$V
curl -L -o /tmp/riscv32-esp-elf.tar.xz \
  https://github.com/espressif/crosstool-NG/releases/download/$V/riscv32-esp-elf-${V#esp-}-$ARCH-apple-darwin.tar.xz
tar -xJf /tmp/riscv32-esp-elf.tar.xz -C ~/.espressif/tools/riscv32-esp-elf/$V
```

**Check it worked:**

```bash
~/.espressif/tools/riscv32-esp-elf/esp-16.1.0_20260609/riscv32-esp-elf/bin/riscv32-esp-elf-gcc --version
```

You should see `riscv32-esp-elf-gcc (crosstool-NG esp-16.1.0_20260609) 16.1.0`.

> 💡 You don't need to add it to your `PATH`: the tikuOS Makefile looks in
> `~/.espressif/tools/riscv32-esp-elf/` by itself and picks the newest version
> it finds there.

> ⚠️ Homebrew's `riscv64-elf-gcc` is **not** a substitute. It has no C library,
> so the kernel build stops at the first `#include <string.h>`.

---

## Step 2 — Install `esptool` (the loading helper)

```bash
brew install esptool
```

**Check it worked:**

```bash
esptool version
```

You should see `5.x` or newer. Older versions don't know the ESP32-C61.

---

## Step 3 — Build tikuOS for the board

From inside the `tikuOS` folder:

```bash
make MCU=esp32c61
```

When it finishes you'll see a **Build Summary** and two new files:

- `main.elf` — the program (with debug info)
- **`main.bin`** — the image `esptool` loads 👈

**✅ Checkpoint:** run `ls main.bin` — if it prints `main.bin`, the build worked.

---

## Step 4 — Keep a copy of the factory flash

The board ships with Espressif's LED-blink demo in its flash, and Step 5
replaces it. A backup takes a minute (use your own `usbmodem` name):

```bash
esptool -p /dev/cu.usbmodem1201 read-flash 0 0x200000 factory_flash_2MB.bin
```

To put the demo back later:

```bash
esptool -p /dev/cu.usbmodem1201 write-flash 0 factory_flash_2MB.bin
```

---

## Step 5 — Open the console, then install tikuOS

Use two Terminal tabs.

**Tab 1 — the console.** Open the **UART** port at 115200 baud:

```bash
screen /dev/cu.usbserial-110 115200
```

(Use your own `usbserial` name from "What you need".) The screen may stay blank
for now. That's expected.

**Tab 2 — install tikuOS.** From the `tikuOS` folder:

```bash
make MCU=esp32c61 flash
```

`esptool` finds the **USB** port by itself, writes the image to the start of
the flash and resets the chip, which boots it. Tab 1 shows the tikuOS banner
and a prompt:

```
  ESP32-C61  |  SRAM 327680B  flash 8192KB
  Type 'help' for commands.

tikuOS:/>
```

**✅ Checkpoint:** you see `tikuOS:/>`. Try a few commands:

```
info
ps
free
freq probe
```

To leave `screen`, press **Ctrl-A**, then **K**, then **Y**.

> 🧠 **From now on the board boots tikuOS by itself** — unplug it, plug it
> into a USB charger, press RESET: the chip's boot ROM copies tikuOS from flash
> into its RAM and runs it. Files you write under `/data` stay too.

> ⚡ **The fast loop.** `make MCU=esp32c61 flash RAM=1` loads the image straight
> into RAM and runs it, without touching flash. A reset then boots whatever is
> in flash again.

> 🔌 **The console and resets.** The UART bridge's control lines are wired to
> the chip's reset pin. A terminal that opens the port normally (like `screen`)
> leaves the chip alone. A tool that drops the DTR line while holding RTS
> restarts the chip, which wipes a tikuOS loaded with `RAM=1`.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `make` says the RISC-V compiler is missing | Redo Step 1, and check the folder name is exactly `~/.espressif/tools/riscv32-esp-elf/esp-16.1.0_20260609/`. |
| `esptool` reports `Resource busy` | The **USB** port re-appears each time the chip resets. Wait a second and run `make MCU=esp32c61 flash` again. |
| `make flash` can't find the board | With the **USB** port missing, `make flash` uses the **UART** port instead (slower, same result). If neither is found, name one: `make MCU=esp32c61 flash ESP_PORT=/dev/cu.usbmodem1201`. |
| The console shows Espressif's boot log, then blink messages | The factory demo is still in flash: run `make MCU=esp32c61 flash`. |
| The console shows nothing at all | Check the console is on the **UART** port and set to 115200 baud. |
