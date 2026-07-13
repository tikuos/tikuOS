# Host-Side Tools on a Mac — TikuBench & tikuConsole

The board guides get firmware onto a chip. **These tools run on your Mac** and
talk to the board over USB:

- **TikuBench** — builds/flashes firmware, runs the on-device test & benchmark
  suites, and reports pass/fail. Has a terminal UI and an optional desktop GUI.
- **tikuConsole** — an interactive serial console + a network bridge (gives the
  board real internet over the USB cable). Has a command-line and a GUI version.

> 🧭 Do [Part 0 of the README](README.md) first (Command Line Tools + Homebrew),
> and set up at least one [board guide](README.md) so you can actually flash.

**The whole dependency list on macOS is tiny:**

| Tool | Needs | Install |
|---|---|---|
| TikuBench harness (Python) | `pyserial` | `python3 -m pip install --user pyserial` |
| TikuBench GUI (C/GTK4) | `gtk4`, `pkg-config` | `brew install gtk4 pkg-config` |
| tikuConsole CLI (native C) | **nothing** (Xcode CLT only) | — |
| tikuConsole GUI (native C/GTK4) | `gtk4`, `pkg-config` | `brew install gtk4 pkg-config` |
| tikuConsole BLE | **nothing** (CoreBluetooth built in) | — |

That's it — `pyserial` + `gtk4` + `pkg-config`. (On macOS you do **not** need
PyGObject/`python3-gi` or `bleak` that the Linux instructions mention — see the
tikuConsole section for why.)

---

## Part A — TikuBench harness (Python)

This is the tool that builds a test firmware, flashes it, watches the UART, and
prints pass/fail. Its only dependency is **pyserial**.

### A.1 — Install pyserial (one line, no venv, no sudo)

```bash
python3 -m pip install --user pyserial
```

Done. Two harmless warnings may appear — one about a scripts folder "not on
PATH", one about your pip version being old. **Ignore both**; pyserial installs
fine regardless.

**Check it worked:**

```bash
python3 -c "import serial; print('pyserial', serial.__version__)"
```

You should see `pyserial 3.5` (or similar). ✅

### A.2 — Run a suite on a board

`python3 -m tikubench` finds the tool through the current folder, so run it
**from inside the `TikuBench` folder**:

```bash
cd ~/tikuOS/TikuBench          # your repo path — this folder holds tikubench/

python3 -m tikubench list      # list every suite

ls /dev/cu.usbmodem*           # note your board's serial port, then:
python3 -m tikubench run kernel --board apollo4l --flash \
        --port /dev/cu.usbmodem0011600012821 --baud 115200 --only process
```

- `--board` is one of: `msp430fr5994` `msp430fr6989` `rp2350` `apollo510`
  `apollo4l` `apollo4p` `apollo510b`.
  > ⚠️ All Ambiq EVBs share one SEGGER USB ID, so auto-detect guesses
  > `apollo510`. **Pass `--board` explicitly** for a Lite/Plus/Blue.
- `--flash` builds + flashes first (via the J-Link / picotool / eZ-FET path your
  board guide set up). Drop it to just monitor an already-flashed board.
- Drop `--only process` to run every non-destructive category for that board.

> 💡 **Want to run it from any folder** (not just `TikuBench/`)? Prefix the path:
> ```bash
> PYTHONPATH=~/tikuOS/TikuBench python3 -m tikubench list
> ```

---

## Part B — TikuBench desktop GUI (optional)

A click-instead-of-type front-end. It's a small **GTK4 program written in C**;
`python3 -m tikubench gui` compiles it on first run and launches it, so it needs
GTK4 present:

```bash
brew install gtk4 pkg-config
cd ~/tikuOS/TikuBench
python3 -m tikubench gui
```

`gtk4` is a biggish install (it pulls in its own libraries), so give it a minute.
The GUI just drives the same `tikubench run …` under the hood.

---

## Part C — tikuConsole (serial console + networking bridge)

tikuConsole gives you an interactive shell over the USB cable **and** bridges that
same wire onto IP, so your Mac can `ping`/`curl` the board while you type at it.

> ⚠️ **Use the macOS build, not the Python script.** The `tikuConsole/*.py`
> files are the **Linux** version (they need `/dev/net/tun` + `iptables`, which
> macOS doesn't have). macOS has its own native build under **`tikuConsole/mac/`**
> that uses a `utun` device + IOKit + CoreBluetooth — all built into macOS.

### C.1 — The command-line bridge (`slmux`) — zero dependencies

Needs nothing but the Xcode Command Line Tools you already have:

```bash
make -C tikuConsole/mac slmux
```

**Check it worked:** `ls tikuConsole/mac/slmux` shows the built binary. ✅

Run it (the network bridge needs root to create the `utun` interface):

```bash
sudo ./tikuConsole/mac/slmux --help
```

### C.2 — The desktop GUI (`tikuconsole`) — needs GTK4

```bash
brew install gtk4 pkg-config      # skip if you already did Part B
make -C tikuConsole/mac           # builds BOTH: tikuconsole (GUI) + slmux (CLI)
./tikuConsole/mac/tikuconsole
```

### C.3 — Bluetooth (BLE) console — nothing to install

Boards with a BLE radio (Apollo510 Blue) expose the shell over Bluetooth. The
macOS build talks to them via CoreBluetooth (built into macOS). Smoke-test it:

```bash
make -C tikuConsole/mac ble_test
./tikuConsole/mac/ble_test tikuOS     # with `ble uart` running on the board
```

---

## One-shot: everything the host tools need

```bash
# 1. Python harness — its only dependency
python3 -m pip install --user pyserial

# 2. GTK4 for the two optional desktop GUIs
brew install gtk4 pkg-config

# 3. tikuConsole native mac build (CLI + GUI + BLE)
make -C tikuConsole/mac
```

Then run TikuBench from the `TikuBench` folder with `python3 -m tikubench …`.

---

## Cheat sheet

| I want to… | Command (run TikuBench ones from the `TikuBench` folder) |
|---|---|
| Install the harness dependency | `python3 -m pip install --user pyserial` |
| List all suites | `python3 -m tikubench list` |
| Run a suite + flash | `python3 -m tikubench run <suite> --board <board> --flash --port <port> --baud <baud>` |
| Launch the TikuBench GUI | `python3 -m tikubench gui` (needs `gtk4`) |
| Build tikuConsole CLI | `make -C tikuConsole/mac slmux` |
| Build tikuConsole GUI | `brew install gtk4 pkg-config && make -C tikuConsole/mac` |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `No module named tikubench` | Run it from inside the `TikuBench` folder (`cd ~/tikuOS/TikuBench`), or prefix `PYTHONPATH=~/tikuOS/TikuBench`. |
| `ModuleNotFoundError: serial` | `python3 -m pip install --user pyserial` (make sure it's the same `python3` you run the tool with). |
| `pip` complains about editable / `setup.py not found` | You don't need an editable install — just `pip install --user pyserial`. (That error only appears with `pip install -e` on an old pip.) |
| Harness finds no port | Pass it explicitly: `--port $(ls /dev/cu.usbmodem*)`, and make sure the board is plugged in. |
| It flashes/monitors the wrong Ambiq board | Auto-detect can't tell Ambiq EVBs apart — always pass `--board apollo4l` (or `apollo4p` / `apollo510` / `apollo510b`). |
| `make: pkg-config: Command not found` when building a GUI | `brew install pkg-config gtk4`; ensure `/opt/homebrew/bin` is on `PATH`. |
| `Package gtk4 was not found` | `brew install gtk4`; if still failing, `export PKG_CONFIG_PATH="$(brew --prefix)/lib/pkgconfig"`. |
| `slmux` won't bring up networking | It needs root: `sudo ./tikuConsole/mac/slmux …` (creating a `utun` device requires it). |
| You ran `tikuconsole.py` and it errored about `tun`/`iptables` | That's the Linux script — use the `tikuConsole/mac/` build instead (Part C). |
