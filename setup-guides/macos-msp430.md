# tikuOS on TI MSP430 (FR5994 / FR6989) — Mac Setup Guide

This guide gets tikuOS building and running on a **TI MSP430 LaunchPad** — the
**MSP-EXP430FR5994** or **MSP-EXP430FR6989**. MSP430 is a 16-bit chip (older and
simpler than the Arm boards), and its Mac toolchain takes a couple of extra
steps. We'll take them slowly.

> 🧭 **New here?** Do [Part 0 of the README](README.md) first (Command Line
> Tools + Homebrew). This guide assumes it's done.

> ⚠️ **Read this first — the honest version.** *Building* tikuOS for MSP430 on a
> Mac works great. *Flashing* is the tricky part: TI only ships the MSP430
> flashing library for Windows and Linux — **there is no official Apple-Silicon
> build**. So on a Mac we flash with **TI's UniFlash app** (Step 4), which is
> actually the easiest route anyway. Don't worry — we'll walk you through it.

**Time needed:** about 30 minutes (mostly downloads).

---

## Which boards does this cover?

| Your LaunchPad | Use this `MCU=` value |
|---|---|
| MSP-EXP430**FR5994** | `msp430fr5994` |
| MSP-EXP430**FR6989** | `msp430fr6989` |

`msp430fr5994` is the default, so a bare `make` builds for it. The examples below
use it — swap in `msp430fr6989` if that's your board.

---

## What you need

- An **MSP430 FR5994 or FR6989 LaunchPad**.
- The **micro-USB data cable** that came with it (plugs into the LaunchPad's
  eZ-FET side).
- About 100 MB of downloads (TI's compiler + UniFlash).

> 🧠 **What's "eZ-FET"?** It's the on-board debugger built into the top half of
> the LaunchPad. It's how your Mac both flashes the chip *and* reads its serial
> console. The USB cable plugs into it.

---

## Step 1 — Turn on Rosetta (one command)

TI's MSP430 compiler is an older program built for Intel Macs. Apple Silicon
runs Intel programs through a translation layer called **Rosetta**. Turn it on:

```bash
softwareupdate --install-rosetta --agree-to-license
```

If it says Rosetta is already installed, perfect — move on.

---

## Step 2 — Install the MSP430 compiler (TI MSP430-GCC)

This is the compiler that turns tikuOS into MSP430 code. It isn't in Homebrew,
so we download it from TI. The tikuOS build expects to find it in a folder
called **`~/tigcc`**, so we'll set that up.

1. Open this page in your browser:
   **https://www.ti.com/tool/MSP430-GCC-OPENSOURCE**

2. Find the downloads and get the one named roughly:
   **"MSP430-GCC — macOS installer, *including support files*"**
   (about 45 MB, version 9.3.1.x).

   > ⚠️ Get the **"including support files"** one — **not** "toolchain only".
   > The support files are the per-chip headers and memory maps tikuOS needs.

3. Run the installer you downloaded. When it asks where to install, note the
   folder it uses (often something like `~/ti/msp430-gcc`).

   > 🔒 **macOS blocks it as "unverified"?** Right-click the installer → **Open**,
   > then confirm. Or clear the warning flag first:
   > ```bash
   > xattr -dr com.apple.quarantine ~/Downloads/<the-installer-you-downloaded>
   > ```

4. Point `~/tigcc` at wherever it installed. Replace the example path with the
   real one from step 3 (the folder that contains `bin` and `include` inside it):

   ```bash
   ln -s ~/ti/msp430-gcc ~/tigcc
   ```

   > 🧠 This `ln -s` makes a shortcut called `~/tigcc`. tikuOS looks there
   > automatically, so you won't have to type long paths later.

**Check it worked — both of these must succeed:**

```bash
~/tigcc/bin/msp430-elf-gcc --version     # should print a gcc version
ls ~/tigcc/include/msp430.h              # should print the path (file exists)
```

If the second command says "No such file or directory", your `~/tigcc` shortcut
is pointing at the wrong folder — it must contain a `bin/` **and** an `include/`
folder. Re-do step 4 pointing at the right place.

> 💡 **Prefer not to use a shortcut?** You can instead tell the build the path
> directly every time: `make MCU=msp430fr5994 TOOLCHAIN_DIR=~/ti/msp430-gcc`.

---

## Step 3 — Build tikuOS for the MSP430

From inside the `tikuOS` folder:

```bash
make MCU=msp430fr5994
```

(or just `make`, since FR5994 is the default). When it finishes you'll see a
**Build Summary** and a `main.elf` file — that's your compiled OS.

**✅ Checkpoint:** run `ls main.elf`. If it prints `main.elf`, **the hardest part
is done** — your Mac can now compile tikuOS for MSP430. 🎉

---

## Step 4 — Flash it onto the LaunchPad (with UniFlash)

Because TI's flashing library isn't available for Apple Silicon, we use **TI
UniFlash** — a free, official app that includes its own drivers and works
natively on Mac. This is the reliable, beginner-friendly way to flash MSP430 on
a Mac.

1. Download **UniFlash** for macOS from:
   **https://www.ti.com/tool/UNIFLASH** — install it like any other Mac app.

2. Plug your LaunchPad into the Mac with the USB cable.

3. Open UniFlash. It should **auto-detect** your board (e.g. "MSP430FR5994").
   Click **Start**.

4. In the **Program** tab, click **Browse** and select the file tikuOS just
   built:

   ```
   <your tikuOS folder>/main.elf
   ```

5. Click **Load Image** (or **Flash**). Wait for "Program Load completed". Done —
   tikuOS is now running on the chip. 🎉

> 💡 **Prefer a hex file?** Some UniFlash versions like a `.hex`. You can make one:
> ```bash
> ~/tigcc/bin/msp430-elf-objcopy -O ihex main.elf main.hex
> ```
> then load `main.hex` instead of `main.elf`.

> 🧠 **Already use Code Composer Studio (CCS)?** CCS can flash `main.elf` too, the
> same way — it bundles the same drivers as UniFlash.

---

## Step 5 — See it running (serial monitor)

The LaunchPad sends tikuOS's console output back over the same USB cable at
**9600 baud** (MSP430 is slower than the Arm boards — this is expected).

Install a friendly serial terminal:

```bash
brew install picocom
```

Also install `mspdebug` — the Makefile uses it for the `erase` command, and it's
handy to have:

```bash
brew install mspdebug
```

Now watch the console:

```bash
make monitor
```

It auto-detects the LaunchPad's serial port and connects at 9600 baud. You
should see tikuOS boot and a `tikuOS>` prompt. Type `help` and press Enter.

- **To quit picocom:** press `Ctrl-A` then `Ctrl-X`.

> 💡 **"No serial port found"?** The MSP430 LaunchPad shows up as a
> `usbmodem` port. Find it and pass it in:
> ```bash
> ls /dev/tty.usbmodem*
> make monitor PORT=/dev/tty.usbmodemXXXX
> ```

---

## 🎉 You did it

Your everyday loop on MSP430 is:

```bash
make MCU=msp430fr5994     # 1. build
# 2. flash main.elf with UniFlash (or CCS)
make monitor              # 3. watch  (9600 baud)
```

---

## Advanced (optional) — making `make flash` work

tikuOS's `make flash MCU=msp430fr5994` runs `mspdebug tilib`, which needs TI's
`libmsp430` flashing library. As noted, TI doesn't ship that for Apple Silicon,
so `make flash` won't work out of the box on your Mac. Two ways around it, for
the curious:

- **Borrow the library from a CCS install.** If you install Code Composer
  Studio, it contains a `libmsp430.dylib`. Point `mspdebug` at it and
  `make flash` can work. Fiddly; not recommended for beginners.
- **Build `libmsp430` from source** (TI's "MSP Debug Stack"), or install it via
  MacPorts. Also advanced, and Apple-Silicon builds aren't guaranteed.

For everyday work, **UniFlash (Step 4) is simpler and more reliable** — stick
with it unless you specifically want the one-command `make flash` flow.

---

## Cheat sheet

| I want to… | Command |
|---|---|
| Build (FR5994) | `make MCU=msp430fr5994` (or just `make`) |
| Build (FR6989) | `make MCU=msp430fr6989` |
| Make a hex for UniFlash | `~/tigcc/bin/msp430-elf-objcopy -O ihex main.elf main.hex` |
| Flash | use **UniFlash** with `main.elf` (see Step 4) |
| Open the serial console | `make monitor` (9600 baud) |
| Pick the serial port myself | `make monitor PORT=/dev/tty.usbmodemXXXX` |
| Clean build files | `make clean` |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `msp430-elf-gcc: command not found` or build can't find the compiler | Your `~/tigcc` shortcut is missing or wrong. Redo [Step 2](#step-2--install-the-msp430-compiler-ti-msp430-gcc) — `~/tigcc/bin/msp430-elf-gcc --version` must work. |
| Build error: `msp430.h: No such file` | `~/tigcc` points at the wrong folder — it must contain both `bin/` and `include/`. You may have grabbed "toolchain only" instead of "including support files". |
| Compiler won't run / "bad CPU type" | Rosetta isn't installed. Run [Step 1](#step-1--turn-on-rosetta-one-command). |
| Installer "can't be opened, unverified developer" | Right-click → **Open**, or `xattr -dr com.apple.quarantine <installer>`. |
| UniFlash doesn't see the board | Try a different USB cable (must carry data), reseat it, and make sure nothing else (CCS, a serial monitor) is holding the port. |
| `make monitor`: no port found | `ls /dev/tty.usbmodem*` then `make monitor PORT=…`. Close UniFlash first if it's still connected. |
| Garbage characters in the monitor | Wrong speed. MSP430 uses **9600** — that's the default here, so just re-run `make monitor`. |
| `make flash` fails with a `libmsp430` / `tilib` error | Expected on Mac — flash with **UniFlash** instead (Step 4). See the Advanced section. |
