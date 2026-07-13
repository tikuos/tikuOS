# tikuOS on the Raspberry Pi Pico 2 (RP2350) — Mac Setup Guide

This guide gets tikuOS building and running on a **Raspberry Pi Pico 2** or
**Pico 2 W** (the chip is called **RP2350**). This is the easiest board to start
with — no debugger probe, no drivers, no soldering. You just drag a file onto it.

> 🧭 **New here?** Do [Part 0 of the README](README.md) first (Command Line
> Tools + Homebrew). This guide assumes it's done.

**Time needed:** about 15 minutes.

---

## What you need

- A **Raspberry Pi Pico 2** or **Pico 2 W** board.
- A **USB cable that carries data** (not a charge-only cable). If your Pico has a
  micro-USB port, you need a USB-A/-C ↔ micro-USB *data* cable.
- Your Mac. That's it — the Pico has a built-in way to receive firmware.

---

## Step 1 — Install the Arm compiler

The RP2350 has an Arm Cortex-M33 processor, so we need the **Arm cross-compiler**
(`arm-none-eabi-gcc`). One command:

```bash
brew install --cask gcc-arm-embedded
```

This downloads Arm's official toolchain (a few hundred MB — give it a minute).

**Check it worked:**

```bash
arm-none-eabi-gcc --version
```

You should see something like `arm-none-eabi-gcc (Arm GNU Toolchain 15.x) ...`.

> 💡 Already did the **Ambiq/Apollo** guide? Then you already have this compiler —
> skip this step.

---

## Step 2 — Install `picotool` (the flashing helper)

`picotool` is the little program that copies firmware onto the Pico over USB.

```bash
brew install picotool
```

**Check it worked:**

```bash
picotool version
```

You should see a version number (2.x).

> You don't *strictly* need `picotool` — the drag-and-drop method in Step 4
> works without it. But it makes re-flashing much faster, so install it.

---

## Step 3 — Build tikuOS for the Pico

From inside the `tikuOS` folder:

```bash
make MCU=rp2350
```

This compiles the whole OS. When it finishes you'll see a **Build Summary** and
three new files in the folder:

- `main.elf` — the program (with debug info)
- `main.bin` — the raw program
- **`main.uf2`** — the file you actually flash onto the Pico 👈

> 🧠 **What's a `.uf2`?** It's a special file format the Pico understands. When
> the Pico is in "flashing mode" it pretends to be a USB memory stick, and
> copying a `.uf2` onto it installs the firmware. Clever, and beginner-proof.

**✅ Checkpoint:** run `ls main.uf2` — if it prints `main.uf2`, the build worked.

---

## Step 4 — Flash it onto the Pico

There are two ways. **Method A is the most foolproof** — start there.

### Method A — Drag and drop (no tools needed)

1. **Unplug** the Pico from your Mac.
2. **Press and hold** the small white **BOOTSEL** button on the Pico.
3. While still holding it, **plug the USB cable back in.**
4. Let go of the button after a second.
5. A disk named **`RP2350`** appears on your Desktop / in Finder (like a USB
   stick).
6. **Drag `main.uf2` onto that `RP2350` disk** (or copy it there):

   ```bash
   cp main.uf2 /Volumes/RP2350/
   ```

7. The disk automatically ejects and the Pico reboots running tikuOS. Done! 🎉

### Method B — One command with `picotool`

If the Pico is already in BOOTSEL mode (Steps 1–4 above), you can instead run:

```bash
make flash MCU=rp2350
```

This rebuilds if needed and uses `picotool` to load and reboot the board.

> The Makefile tries `picotool` first, and if that's not available it falls back
> to copying the `.uf2` to the mounted `RP2350` disk automatically.

---

## Step 5 — See it running (serial monitor)

Once tikuOS is running, the Pico talks to your Mac over a **serial console** at
**115200 baud**. Let's watch it.

Install a friendly serial terminal (the build prefers this one):

```bash
brew install picocom
```

Then, with the Pico plugged in and running:

```bash
make monitor
```

This auto-detects the Pico's port and connects. You should see tikuOS boot text
and a `tikuOS>` prompt. Type `help` and press Enter to explore.

- **To quit picocom:** press `Ctrl-A` then `Ctrl-X`.

> 💡 **"No serial port found"?** Find the port by name and pass it in:
> ```bash
> ls /dev/tty.usbmodem*
> make monitor PORT=/dev/tty.usbmodemXXXX     # use the name you saw
> ```

---

## 🎉 You did it

You can now build, flash, and watch tikuOS on the Pico. The everyday loop is:

```bash
make MCU=rp2350          # 1. build
# put Pico in BOOTSEL mode, then:
make flash MCU=rp2350    # 2. flash   (or drag main.uf2 onto the RP2350 disk)
make monitor             # 3. watch
```

---

## Optional — Live debugging with a Debug Probe

Drag-and-drop flashing is all most people need. If you want to set breakpoints
and step through code, you need a **Raspberry Pi Debug Probe** (or a second Pico
flashed as one) wired to the 3-pin SWD/debug header. Then:

```bash
brew install openocd     # the debug bridge
make debug MCU=rp2350    # prints the exact openocd command to run
```

> ⚠️ Some Homebrew builds of `openocd` don't yet know about the RP2350. If
> `make debug` fails with "unknown target rp2350", install the Raspberry Pi
> fork of openocd instead. This is an advanced/optional step — skip it while
> you're getting started.

---

## Cheat sheet

| I want to… | Command |
|---|---|
| Build for Pico 2 | `make MCU=rp2350` |
| Flash (Pico in BOOTSEL mode) | `make flash MCU=rp2350` |
| Flash by hand | drag `main.uf2` → `RP2350` disk |
| Open the serial console | `make monitor` |
| Pick the serial port myself | `make monitor PORT=/dev/tty.usbmodemXXXX` |
| Clean build files | `make clean` |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `arm-none-eabi-gcc: command not found` | Re-run `brew install --cask gcc-arm-embedded`, then open a new terminal. |
| No `RP2350` disk appears | You didn't enter BOOTSEL mode. Unplug, **hold BOOTSEL**, plug in *while holding*, then release. |
| Still no disk, and Mac shows nothing | Your USB cable is probably **charge-only**. Try a different cable — it must carry data. |
| `make: command not found` | Do [README Part 0.2](README.md) (Command Line Tools). |
| Build fails with missing headers | Make sure you ran `make` **from inside the `tikuOS` folder** (`pwd` to check). |
| `make monitor` says no port | Board must be *running* (not in BOOTSEL). Then `ls /dev/tty.usbmodem*` and pass `PORT=…`. |
| Garbage characters in the monitor | Wrong speed. tikuOS uses 115200 here — the default is correct, so re-run `make monitor`. |
