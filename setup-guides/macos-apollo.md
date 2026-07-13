# tikuOS on Ambiq Apollo (Apollo510 / Apollo4) — Mac Setup Guide

This guide gets tikuOS building and running on **Ambiq Apollo** evaluation
boards. These are low-power Arm chips. Unlike the Raspberry Pi Pico, they are
flashed with a small hardware gadget called a **SEGGER J-Link**, which most
Ambiq EVBs have built onto the board already.

> 🧭 **New here?** Do [Part 0 of the README](README.md) first (Command Line
> Tools + Homebrew). This guide assumes it's done.

**Time needed:** about 20 minutes.

---

## Which Apollo boards are supported?

Pick the name that matches your board — you'll use it as `MCU=` later:

| Your EVB | Use this `MCU=` value |
|---|---|
| Apollo510 EVB | `apollo510` |
| Apollo510 Blue EVB (with EM9305 radio) | `apollo510b` |
| Apollo4 Lite EVB | `apollo4l` |
| Apollo4 Plus EVB | `apollo4p` |

This guide uses `apollo510` in the examples — just swap in your value.

---

## What you need

- An **Ambiq Apollo EVB** from the table above.
- A **USB data cable** (most Apollo EVBs use USB-C or micro-USB).
- The EVB's on-board **J-Link** (standard on Ambiq dev kits). If your board has
  *no* on-board debugger, you need a separate SEGGER J-Link probe wired to the
  SWD header — but for the standard EVBs, the cable is all you need.

> 🧠 **What's a J-Link?** It's a tiny debugger made by SEGGER. It's the bridge
> that lets your Mac push firmware into the chip and pause/inspect it. On Ambiq
> EVBs it's built onto the board, so plugging in the USB cable is enough.

---

## Step 1 — Install the Arm compiler

Apollo chips use Arm Cortex-M processors, so we need the **Arm cross-compiler**
(`arm-none-eabi-gcc`):

```bash
brew install --cask gcc-arm-embedded
```

**Check it worked:**

```bash
arm-none-eabi-gcc --version
```

You should see `arm-none-eabi-gcc (Arm GNU Toolchain 15.x) ...`.

> 💡 Already did the **Raspberry Pi Pico** guide? You already have this
> compiler — skip this step.

> 🧠 Good news: tikuOS ships the Apollo chip register headers *inside* the
> source tree, so you do **not** need to download Ambiq's big "AmbiqSuite" SDK.

---

## Step 2 — Install the SEGGER J-Link tools

These are the programs that actually talk to the J-Link on your board:

```bash
brew install --cask segger-jlink
```

This installs `JLinkExe` (the flasher) and `JLinkGDBServer` (for debugging).

> ⚠️ **A window may ask for your Mac password** — that's normal for this
> installer. You may also be asked to agree to SEGGER's licence.

**Check it worked:**

```bash
JLinkExe -?
```

If a J-Link help/banner prints (you can press `q` then Enter, or `Ctrl-C` to
exit), you're good.

> 🔒 **macOS blocked it?** If macOS pops up *"cannot be opened / unverified
> developer"*, open **System Settings → Privacy & Security**, scroll down, and
> click **Allow Anyway**, then try again.

---

## Step 3 — Build tikuOS for your Apollo board

From inside the `tikuOS` folder (swap `apollo510` for your board):

```bash
make MCU=apollo510
```

When it finishes you'll see a **Build Summary** and two new files:

- `main.elf` — the program (with debug info, used for debugging)
- `main.bin` — the raw image that gets written to the chip

**✅ Checkpoint:** run `ls main.bin` — if it prints `main.bin`, the build worked.

---

## Step 4 — Plug in and flash

1. Connect the EVB to your Mac with the USB cable. A green power LED usually
   lights up.
2. Flash it (swap in your `MCU=`):

   ```bash
   make flash MCU=apollo510
   ```

That's it. Behind the scenes the Makefile writes a tiny J-Link script, loads
`main.bin` into the chip's memory, resets, and runs it. You'll see J-Link print
its progress and `O.K.` messages.

> 🧠 tikuOS already knows the correct J-Link "device name" for each board
> (e.g. `AP510NFA-CBR` for Apollo510), so you don't have to configure anything.

### Got more than one J-Link plugged in?

J-Link will ask which one to use. Skip the question by giving its serial number:

```bash
make flash MCU=apollo510 JLINK_SN=001160001290
```

(Find the serial number in the flashing output, or on the J-Link/EVB label.)

---

## Step 5 — See it running (serial monitor)

The Apollo EVB sends its console output over the J-Link's built-in serial port
("VCOM") at **115200 baud**. Let's watch it.

Install a friendly serial terminal:

```bash
brew install picocom
```

Then, with the board plugged in and running:

```bash
make monitor
```

It auto-detects the port and connects. You should see tikuOS boot and a
`tikuOS>` prompt. Type `help` and press Enter.

- **To quit picocom:** press `Ctrl-A` then `Ctrl-X`.

> 💡 **"No serial port found"?** List the ports and pass one in:
> ```bash
> ls /dev/tty.usbmodem*
> make monitor PORT=/dev/tty.usbmodemXXXX
> ```

---

## 🎉 You did it

Your everyday loop is:

```bash
make MCU=apollo510          # 1. build   (use your board's value)
make flash MCU=apollo510    # 2. flash
make monitor                # 3. watch
```

---

## Optional — Live debugging (breakpoints, stepping)

The J-Link can pause the chip and let you step through your code. You need
**two terminal windows**.

**Terminal 1 — start the debug server** (the command `make debug` prints the
exact line for your board; it looks like):

```bash
JLinkGDBServer -device AP510NFA-CBR -if SWD -speed 4000
```

**Terminal 2 — connect the debugger** to it:

```bash
arm-none-eabi-gdb main.elf -ex 'target remote :2331' -ex load -ex 'monitor reset' -ex continue
```

Now you can set breakpoints (`break main`), step (`next`), inspect variables
(`print x`), etc. Type `make debug MCU=apollo510` any time to reprint these
commands with the right device name filled in.

> This is optional — you can go a long way with just `make flash` + `make
> monitor` and print statements.

---

## Cheat sheet

| I want to… | Command |
|---|---|
| Build (pick your board) | `make MCU=apollo510` (or `apollo4l` / `apollo4p` / `apollo510b`) |
| Flash | `make flash MCU=apollo510` |
| Flash a specific probe | `make flash MCU=apollo510 JLINK_SN=<serial>` |
| Erase the chip | `make erase MCU=apollo510` |
| Open the serial console | `make monitor` |
| Print the debug commands | `make debug MCU=apollo510` |
| Clean build files | `make clean` |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `arm-none-eabi-gcc: command not found` | Re-run `brew install --cask gcc-arm-embedded`, open a new terminal. |
| `JLinkExe: command not found` | Re-run `brew install --cask segger-jlink`, open a new terminal. |
| macOS won't open the J-Link tools | **System Settings → Privacy & Security → Allow Anyway**, then retry. |
| Flashing: "cannot connect to target" | Check the USB cable carries data, the EVB is powered (LED on), and you picked the right `MCU=` for your board. |
| "Emulator connection lost" / multiple probes | Pass `JLINK_SN=<serial>` to pick one probe. |
| `make monitor` finds no port | Board must be running. `ls /dev/tty.usbmodem*`, then pass `PORT=…`. |
| Garbage in the monitor | Speed mismatch — Apollo uses 115200, which is the default, so just re-run `make monitor`. |
| Wrong chip name in J-Link errors | Double-check the `MCU=` value matches your exact board from the table at the top. |
