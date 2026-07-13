# Setting up tikuOS on a Mac — Toolchain Guides

Welcome! 👋 These guides walk you, step by step, through installing everything
your Mac needs to **build, flash, and run tikuOS** on real hardware.

They are written for beginners. If you have never touched a microcontroller or a
terminal before, that's fine — we explain every command and tell you what you
should see after each step. Copy, paste, press Enter, check the result, move on.

> **Status:** these are internal drafts. They will be polished and published on
> the tikuOS homepage later. This whole folder is git-ignored on purpose.

---

## Which guide do I need?

tikuOS runs on three different families of chips. Pick the board you actually
have in your hand:

| I have this board… | Chip family | Open this guide |
|---|---|---|
| TI **MSP430** LaunchPad (FR5994 or FR6989) | MSP430 (16-bit) | [macos-msp430.md](macos-msp430.md) |
| **Raspberry Pi Pico 2** / Pico 2 W | RP2350 (Arm Cortex-M33) | [macos-rp2350-pico.md](macos-rp2350-pico.md) |
| **Ambiq Apollo** EVB (Apollo510, Apollo4 Lite/Plus, Apollo510 Blue) | Apollo (Arm Cortex-M4/M55) | [macos-apollo.md](macos-apollo.md) |

You can install more than one — they don't conflict. The Raspberry Pi and Ambiq
guides even share the same Arm compiler, so if you set up one, the other is
almost done already.

**Plus — host-side tools that run on your Mac and talk to the board:**

| I want to… | Open this guide |
|---|---|
| Run the test / benchmark suites (**TikuBench**) or the serial console + network bridge (**tikuConsole**) | [macos-host-tools.md](macos-host-tools.md) |

The whole extra dependency list there is small: `pyserial` (test harness) plus
`gtk4` + `pkg-config` (only if you want the desktop GUIs).

**Do the "one-time setup" below first.** Every guide assumes you've done it.

---

## Part 0 — One-time setup (do this once, ever)

These four things are needed no matter which board you use. If you've built
other software on this Mac before, you may already have some of them — the
guide tells you how to check.

### 0.1 — Confirm you're on an Apple Silicon Mac

Every recent Mac (M1/M2/M3/M4…) is "Apple Silicon". Let's confirm:

```bash
uname -m
```

- `arm64` → Apple Silicon. ✅ These guides are written for you.
- `x86_64` → older Intel Mac. The steps still mostly work, but a couple of paths
  differ (Homebrew lives in `/usr/local` instead of `/opt/homebrew`). Ask a
  maintainer if you get stuck.

### 0.2 — Install Apple's Command Line Tools

This gives you `git`, `make`, and a C compiler — the basic build machinery.

```bash
xcode-select --install
```

A little window pops up — click **Install** and wait for it to finish. If it says
*"command line tools are already installed"*, great, you already have them.

**Check it worked:**

```bash
git --version && make --version
```

You should see version numbers, not "command not found".

### 0.3 — Install Homebrew (the "app store for the terminal")

Homebrew (`brew`) is how we install most tools. If you already have it,
skip to the check at the end.

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

At the end it may print two lines starting with `eval "$(...)"`. **Run them.**
If it doesn't, run these to make sure `brew` works in every new terminal window:

```bash
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

**Check it worked:**

```bash
brew --version
```

You should see `Homebrew 4.x.x`.

> 💡 **"command not found: brew" in a new window?** You skipped the two lines
> above — run them again. They tell your Mac where Homebrew lives.

### 0.4 — Confirm you have Python 3

The build uses a couple of small Python helper scripts. macOS already ships
Python 3, so you almost certainly have it:

```bash
python3 --version
```

Anything like `Python 3.9` or newer is fine. Nothing to install.

### 0.5 — Get the tikuOS source code

If you haven't already, download the code and step into its folder:

```bash
git clone <the tikuOS repository URL> tikuOS
cd tikuOS
```

Every `make` command in these guides is run **from inside this `tikuOS`
folder**. If a command says "file not found", you're probably in the wrong
folder — run `pwd` to see where you are, and `cd` back into `tikuOS`.

---

## ✅ Checkpoint

Before opening a board guide, all of these should print a version number
without errors:

```bash
uname -m          # arm64
git --version
make --version
brew --version
python3 --version
```

All good? Great — open the guide for your board and let's flash some firmware. 🚀

---

## A 30-second glossary (jargon we'll use)

- **Toolchain / cross-compiler** — a special compiler that runs on your Mac but
  produces code for a *different* chip (the microcontroller). Each chip family
  needs its own.
- **Flash** — copying your compiled program onto the chip so it runs.
- **Serial / UART monitor** — a text window that shows messages printed by the
  board (its "console"), and lets you type commands back.
- **Baud rate** — the speed of that serial connection. MSP430 uses `9600`;
  the Raspberry Pi and Ambiq boards use `115200`. The guides set this for you.
- **BOOTSEL, eZ-FET, J-Link** — the little on-board (or plug-in) helpers that
  let your Mac talk to the chip. Each board family has its own; the guides
  explain them.
