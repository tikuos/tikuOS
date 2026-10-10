# ESP32-C5 native port

The C5 target runs the kernel, VFS, shell, file store and optional worker
threads over its native USB Serial/JTAG port. A lean BASIC interpreter is
available with `TIKU_SHELL_BASIC_ENABLE=1`; ADC and I2C words are disabled.
The default profile runs code and working data from HP SRAM. Optional XIP
places BASIC and shell commands in flash. CPU rates of 40, 80 and 240 MHz
can be saved through VFS and applied on reboot. UART0 is an optional console;
its internal loopback is tested, but an external adapter is not qualified.
An I2C0 backend is available through the C bus API; its external-device
transfers still require hardware qualification. An optional C5 PHY driver
supports calibration and RF lifecycle diagnostics. Cached PSRAM and an opt-in
native Wi-Fi, BLE and receive-only SDR adapters are implemented; their
qualification limits are described below and in the C5 driver README.
SPI, ADC, 1-Wire and deep sleep are not supported. Requests for unsupported optional
features fail at build selection; a C61 platform macro is not a substitute.

## Package memory and radio diagnostics

The package PSRAM capacity code can be read with
`tiku_cpu_c5_psram_package_code()`. Zero reports no in-package PSRAM. A
nonzero code does not register memory with the allocator: bus initialization,
capacity/alias tests, cache configuration and mapping are separate operations.
Do not infer fitted PSRAM from the C5 family name or from a DevKit build selection.

`tiku_c5_psram_attach()` detects supported AP Memory quad-SPI devices, checks
capacity using patterns separated by 64 KiB, maps cached read/write memory at
`0x42800000`, and registers it as external working memory. The mapping is not
executable. The supported densities are 2, 4 and 8 MiB; an 8 MiB 2T part exposes
4 MiB. The driver leaves the existing flash clock source unchanged and uses
the same divider for RAM: 20/24 MHz from XTAL, or 40 MHz from PLL480. Other
clock profiles, DDR/octal modes, ECC and occupied mapping/PMA entries are refused.
This profile does not enable encrypted PSRAM or qualify peripheral DMA access.

Use `/sys/psram/state` to request `up` or `down`; `size`, `id`, `package_code`
and `result` report detection and mapping. A missing package returns `absent`
without changing the controller. A board with verified discrete PSRAM can opt
in with `EXTRA_CFLAGS=-DTIKU_C5_PSRAM_EXTERNAL=1`; this is not automatic probing
of an unidentified board. Calls that change the mapping require kernel
foreground with interrupts enabled. Shutdown refuses live allocations and a
replacement external-memory tier. An unexpected stalled controller halts the
board because returning to flash with a transaction pending is unsafe.

The connected board reports package code zero. TikuBench `psram-c5` passed its
13 absent-device checks; host models exercise successful mapping, capacity,
cache boundaries, allocation ownership, rollback and forced-detach recovery.
Positive PSRAM hardware qualification still requires a populated board.

The optional [`C5 PHY component`](../../drivers/wifi/esp/c5/README.md) has its
own pinned vendor assets and `TIKU_DRV_PHY_C5_ENABLE=1` build switch. RF stays
off at boot. Its explicit `phy-c5` TikuBench diagnostic exercises calibration,
warm wake and shutdown without starting a MAC or joining a network. Active
PHY ownership prevents SAR entropy acquisition and live CPU-clock changes.
The optional native Wi-Fi adapter uses this PHY plus the shared TikuOS worker,
timer, heap and WPA2 adapters. See the C5 component README for its build and
scan qualification command. BLE and SDR have separate opt-in build profiles.
Simultaneous Wi-Fi/BLE operation is refused; SDR requires exclusive PHY
ownership and a free 128 KiB SRAM capture bank. TikuBench exposes `bt` and
`sdr` suites for C5 in both its CLI and GUI. See the component README for
build commands, receiver limits and the distinction between controller
tests and over-the-air qualification.

## Kernel build and installation

```sh
make MCU=esp32c5 TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESPTOOL=/path/to/venv/bin/esptool
```

This produces `build/esp32c5/main.elf` and `main.bin`. Add
`TIKU_THREADS_ENABLE=1` for stackful compute workers. There is no C-library
heap: `errno` is isolated per worker and interrupt, but library calls that
require allocation are not supported. Worker confinement rules in
`kernel/threads/tiku_thread.h` still apply.

After backing up the board, choose either a RAM run or installation:

```sh
/path/to/venv/bin/python tools/esp32c5_ram.py --profile kernel \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    build/esp32c5/main.bin
/path/to/venv/bin/python tools/esp32c5_flash.py \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    build/esp32c5/main.bin
```

Both loaders reset the selected board. The RAM loader does not issue flash
writes, but the loaded kernel can provision a blank store and update
persist cells. A RAM kernel is therefore **not** a read-only storage test.
The flash installer checks chip, revision, security state and 4 MiB flash
geometry, writes only the boot-image extent at `0x2000`, verifies it, and
requests reset. It does not erase the storage partitions. Use
`--no-monitor` for a firmware image without a shell; that checks programming,
not firmware execution. Whole-chip `make erase` is refused.

## Execute application code from flash

Add `TIKU_ESP32C5_XIP_CODE=1` to a shell/BASIC build. This produces
`main.bin` and `xip.bin` in the selected build directory. Install both:

```sh
/path/to/venv/bin/python tools/esp32c5_flash.py \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    --xip build/esp32c5/xip.bin build/esp32c5/main.bin
```

`make flash` passes the companion automatically. Supply
`ESP_PYTHON=/path/to/venv/bin/python` for the packer and installer. A missing
companion is a pre-write error; the RAM loader refuses XIP builds.

Startup, architecture code, kernel, console transport and interrupt
handlers remain in SRAM. The linker moves BASIC, shell-command and optional
vendor radio ordinary code and constants to `0x42100000`, backed by flash
offset `0x100000`. Vendor IRAM sections stay in SRAM. Their
writable state stays in SRAM. The packer checks allocated ELF section ranges,
the two stacks and named startup, exception, validation and flash routines.
The firmware's flash driver copies each write page into SRAM before suspending
the cache and masks interrupts until the cache has resumed.

The 64-byte header contains a version, payload extent, CRC and SHA-256 pair
identity. The identity covers the addressed SRAM program and complete XIP
payload. The installer validates both images, writes and verifies XIP first,
then writes and verifies the boot image. Startup compares the installed header
with its SRAM descriptor and checks the payload CRC before application code
runs. A stale or damaged pair halts with a USB error. This is integrity checking,
not secure boot or an atomic firmware update: a failed update may require
reinstalling both images through the ROM loader. Storage partitions are untouched.

`main.elf` is the linker output with an unfilled descriptor;
`main.paired.elf` contains the installed descriptors for debugging. Regenerate
the pair after changing either program. Do not install a boot image produced
directly from the unfilled ELF.

## Clock settings

Read `/sys/cpu/freq`, `/sys/cpu/freq_available` and
`/sys/cpu/freq_change_mode`. Write a listed frequency in Hz to
`/sys/cpu/freq_target`, then reboot. For example:

```text
write /sys/cpu/freq_target 80000000
reboot
cat /sys/cpu/freq
```

Saving does not change the running clock. After a cold boot the ROM leaves the
CPU on the 48 MHz crystal with the PLL off; the first clock change powers the
480 MHz PLL, calibrates it from the crystal and moves the CPU onto its 240 MHz
output with AHB at 40 MHz, as ESP-IDF's bootloader does. Later changes set
only the CPU divider. Before the first switch above 80 MHz the clock code sets
the eFuse-calibrated core voltage (the PVT-tracked voltage is not
implemented); 240 MHz is refused if that setting fails, and `diag voltage`
shows it. A PLL that does not calibrate leaves the CPU on the crystal, where
`/sys/cpu/freq_change_mode` reads `fixed` and the radios refuse to start;
`diag clock` shows the PLL result and the clock registers. Unsupported clock
trees are refused. The update has bounded waits, readback checks and rollback;
ROM delay calibration follows the selected CPU rate. A failed durable save is
reported. Device Preferences uses these same VFS controls.

## I2C master

The C bus API controls I2C0 at 100 or 400 kHz using the 48 MHz crystal.
The default SDA/SCL pins are GPIO8/GPIO9. Neither is configured at boot:
`tiku_i2c_init()` claims them and `tiku_i2c_close()` restores their previous
mux, matrix routing, open-drain setting and output-enable state. Both pins
need external pull-ups to 3.3 V and a common ground with the sensor.
USB, memory, strapping, console and already-owned pins cannot be claimed.
Board builds can override `TIKU_BOARD_I2C_SDA_PIN` and
`TIKU_BOARD_I2C_SCL_PIN`; these are physical GPIO numbers, not virtual ports.

```c
#include <interfaces/bus/tiku_i2c_bus.h>

int read_sensor_register(uint8_t address, uint8_t reg, uint8_t *value)
{
    tiku_i2c_config_t config = { TIKU_I2C_SPEED_STANDARD };
    int result = tiku_i2c_init(&config);
    if (result != TIKU_I2C_OK) {
        return result;
    }
    result = tiku_i2c_write_read(address, &reg, 1, value, 1);
    tiku_i2c_close();
    return result;
}
```

The address is seven-bit and unshifted. `write_read` inserts a repeated
START between the write and read; it does not insert a STOP there. Reads
NACK their final byte. `tiku_i2c_probe()` sends an address without a data
byte. Reads and writes accept 1–65535 bytes and divide them into 32-byte
FIFO batches without releasing the bus between batches. A long transfer
blocks the cooperative scheduler; choose lengths and watchdog settings
accordingly. These calls require serialized scheduler context, not an ISR
or compute worker.

`NACK` means a device did not acknowledge; `BUSY` includes pin/controller
ownership conflicts and arbitration loss. `TIMEOUT` covers a stalled
controller, SCL stretching beyond approximately 2.73 ms, FIFO faults or
failure to finish a batch within 50 ms. A fixed poll limit also prevents
an infinite wait when the timebase stops. Errors are not retried, and a
write may already have reached the device or a read may have filled a
prefix of the buffer. On failure the driver resets its state machine;
it does not send nine recovery clocks. A failed reset retains the pin
claims and rejects further transfers until close/reinitialization.

The `i2c-c5` TikuBench category checks the real controller at both bus
rates and all three CPU rates. It disconnects output pads before transfers
and uses constant matrix inputs to test stalled START/SCL handling,
bounded return, re-entry after recovery and full pin restoration. The host
model tests successful transfers, repeated STARTs, FIFO boundaries,
arbitration, NACK, partial progress, late inter-batch faults and failure
recovery, including rejected implementation mutations. These checks do not
qualify a sensor exchange, external pull-ups, physical clock accuracy or
multi-master operation. BASIC's I2C words remain disabled in this profile.
For manual C/shell qualification, the existing shell command can be built
with `EXTRA_CFLAGS='-DTIKU_SHELL_CMD_I2C=1'`.

Register/timing definitions follow the pinned C5
[I2C HAL reference](https://github.com/espressif/esp-idf/blob/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_hal_i2c/esp32c5/include/hal/i2c_ll.h).

## UART console and internal loopback

Native USB remains the default. For an external 3.3 V UART adapter, add
`TIKU_CONSOLE=uart` to the kernel build; TX is GPIO11, RX is GPIO12 and the
adapter needs a common ground. The default is 115200 baud, 8 data bits,
no parity and one stop bit. `UART_BAUD=...` accepts 300 through 3000000.
Do not connect RS-232 voltage levels to these pins.

UART0 uses the board's 48 MHz crystal, independent of the selected CPU
divider. The boot halts with a message on a part with any other crystal.
Receive interrupts copy the 128-byte FIFO into a 1024-byte ring
with 1023 usable positions. When the ring fills, earlier bytes are retained
and new bytes are dropped and counted. FIFO-overflow events also increment
the saturating overrun counter. The receive functions can drain the FIFO
with interrupts masked. Register synchronization and transmit waits are
bounded; initialization failure prevents boot from proceeding with an
unavailable console.

The selected UART console reserves GPIO11/12 against GPIO operations.
`tiku_c5_uart_start(baud, 1)` selects internal loopback and disconnects the
UART TX pad; stopping restores the changed routing. A competing interrupt
owner or alternate UART TX matrix route causes initialization to fail.
`tiku_c5_uart_stats()` reports receive interrupt events, overruns,
framing/parity events and rejected transmit bytes. `wake` reports UART RX
when its interrupt source, line and receive interrupts are enabled.

Flash installation still uses the native USB port (`ESP_PORT=...`), not the
external adapter. `make flash ... TIKU_CONSOLE=uart` verifies installation
but does not monitor the shell through USB. Open the separate UART adapter
at the selected baud after boot. Terminal fault reports remain available on
native USB even in a UART-console build. The RAM diagnostic is USB-only.

The TikuBench and desktop Firmware tools remain USB-only: their single
selected port serves programming and console verification. They reject a
UART-console override before building or flashing. Build a UART image with
`make` directly; UART electrical timing, adapter traffic and UART-only boot
recovery still require qualification with a connected adapter.

The `uart-c5` TikuBench category tests internal loopback through the real
UART while results travel over USB. It transfers all byte values at 9600,
115200, 921600 and 3000000 baud with CPU rates of 40, 80 and 240 MHz. It also
checks receive interrupts, ring and hardware FIFO overflow, masked-interrupt
polling, recovery, pin restoration and wake reporting. It restores the CPU
clock and does not change saved settings. For example:

```sh
python -m tikubench run kernel --board esp32c5 --console usb --only uart-c5 \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    --flash --make-var TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    --make-var TOOLCHAIN_PREFIX=riscv-none-elf- \
    --make-var ESPTOOL=/path/to/venv/bin/esptool
```

Register definitions follow the pinned [C5 UART HAL](https://github.com/espressif/esp-idf/blob/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_hal_uart/esp32c5/include/hal/uart_ll.h)
and the C5 UART/PCR/IO-mux headers from the same revision, not C61 bindings.

## Memory and storage

The app SRAM window is `0x40800000..0x4084E5A0`. Its upper bound preserves
the C5 ROM workspace. A 16 KiB main stack ends at that bound and a 2 KiB
ISR stack belongs to BSS. The common SRAM linker fragment exposes the
remaining contiguous space to the allocator, with a 32 KiB minimum.
LP SRAM is not allocated. `.retained` is separate from the durable mirror.

Offsets below are relative to the start of the identified 4 MiB flash:

| Offset range | Purpose |
| --- | --- |
| `0x000000..0x100000` | Boot-image budget; ROM image starts at `0x2000` |
| `0x100000..0x200000` | Optional executable XIP image and its header |
| `0x200000..0x3F7000` | NVM region: allocation tier followed by `/data` |
| `0x3F7000..0x3F8000` | Destructive-test scratch sector |
| `0x3F8000..0x3FC000` | Durable mirror A |
| `0x3FC000..0x400000` | Durable mirror B |

The region backend preserves neighboring bytes when a sector needs erasure
and propagates ROM/readback errors. Such a read-modify-erase operation is
not power-cut atomic for the whole sector. Mirror commits use alternating
CRC-checked slots, write the commit marker last, and retain the latest valid
slot during the next update. The 16-byte header counts against each slot.
Oversized durable sections fail the link rather than being truncated.

`nvm.tier` uses the common staged-layout protocol. The default is 32 KiB;
`/data` has 4 KiB sectors and 4092-byte slot payloads. Automatic provisioning
requires a blank region and uses the C5 SAR entropy configuration. The
entropy driver refuses an active ADC, restores its saved configuration,
and bounds hardware waits. Radio-enabled entropy operation is unqualified.

The PMP null-address guard does not implement SRAM write protection.
Durable write windows control commit ordering; software can still write
that SRAM outside a window. Reboot-survival checks do not establish
physical power-cut durability.

GPIO ownership protects USB, memory, LED and documented strap pads, direct
peripheral mux functions and active matrix input/output routes. GPIO IRQs
return failure. Pin loopbacks and external bus fixtures are unqualified.

An exception is printed on the console, recorded in retained SRAM and resets
the board; `tiku_c5_fatal()` (an unrecoverable peripheral failure) does the
same with its reason text. `diag fault` shows the record. After three such
resets without two seconds of uptime between them the next fault halts
instead, so a fault on every boot does not loop; `diag fault fatal` exercises
the path.

The board's addressable RGB LED (GPIO27, GRB order) is the kernel's three
LEDs: `/dev/led0`, `led1` and `led2` switch its red, green and blue channels
at 16 of 255. Each change sends a 24-bit frame timed from the CPU cycle
counter with interrupts masked for about 30 us.
Idle uses WFI with the USB PHY powered; there is no deep-sleep profile.

The USB console sends a delayed zero-length packet when a full 64-byte
packet ends a transfer. The hardware auto-flushes a full FIFO, but the host
can retain that last packet until a short packet or zero-length packet
terminates the transfer. Foreground and idle polling retry completion
without waiting for the host.

## RAM diagnostic

`MCU=esp32c5 MINIMAL=1` builds a standalone RAM diagnostic. The board
selection is `BOARD=esp32c5_devkitc1`; its console is native USB Serial/JTAG.
Optional features and flashing this diagnostic are refused.

### Diagnostic build

Use a RISC-V GCC with RV32IMAC support and `esptool==5.1.0`:

```sh
make MCU=esp32c5 MINIMAL=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESPTOOL=/path/to/venv/bin/esptool
```

The outputs are `build/esp32c5/diagnostic.elf` and `diagnostic.bin`. The
image carries C5 chip ID 23 and requires silicon revision 1.0 or later.
The loader and firmware also require ROM ECO 2 or later. The two revision
numbers are different checks, not interchangeable names.

The diagnostic uses the first 64 KiB of HP SRAM. Its stack occupies the
last 4 KiB of that window. An additional 2 KiB interrupt stack is part of
BSS; the linker rejects a growing image that reaches the main stack.
The rest of HP SRAM and all LP SRAM are untouched. This is a
diagnostic limit, not the kernel's SRAM allocation limit. The diagnostic
does not initialize PSRAM or mount the file store. The image's 4 MiB flash
field describes the identified test board.

### Diagnostic run

Identify the board and back up its readable flash before replacing any
firmware. Opening the ROM loader resets the board. Close other users of
the port; use its persistent by-id path rather than an ACM number.

```sh
/path/to/venv/bin/python tools/esp32c5_ram.py \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    --seconds 8 --reopen 3 --log /path/to/new-capture.log \
    build/esp32c5/diagnostic.bin
```

Alternatively, `make ram MCU=esp32c5 MINIMAL=1 ESP_PORT=...` uses the same
validator. Supply the build toolchain options above and
`ESP_PYTHON=/path/to/venv/bin/python`. This Python environment needs
esptool 5.1.0 and pyserial. Port ownership checks require `fuser`.

The loader validates the complete image before opening the port, checks
the native Espressif USB identity and existing port owners, then confirms
the chip, silicon revision and ROM ECO. It loads through the ROM without
a flasher stub or flash writes. The log file must not already exist.

The diagnostic checks initialized data, zeroed BSS and a small SRAM test
array. It configures the boot watchdog registers and leaves the CPU clock
inherited from the ROM. SYSTIMER unit 0 supplies a 16 MHz counter, with
alarm 0 routed to CLIC line 0 for 128 Hz ticks. Delayed interrupts count
elapsed ticks without a catch-up loop. A failed counter snapshot stops
the tick alarm and increments an error counter. Reinitialization resets
tick accounting and reuses only the driver's own IRQ route.

A separate heartbeat uses the CPU cycle counter and ROM-reported MHz.
The output reports ticks, serviced interrupts, timer read errors and
unclaimed interrupts. These check the low-level timer; the kernel clock
adapter and clock-change controls are not part of this diagnostic.

USB writes accept at most 64 bytes without waiting. A full FIFO returns
zero; the caller retains and retries the suffix. Received bytes remain in
the hardware FIFO until consumed. The diagnostic understands:

| Byte | Response |
| --- | --- |
| `i` | Repeat the memory-test result and identity |
| `p` | `PONG` |
| `c` | Check nested interrupt masking and recovery after 40 ms with IRQs masked |
| `r` | Check register preservation across live timer interrupts |
| `?` | Command help |
| `t` | Execute `ebreak`, report the terminal trap, and halt |
| `f` | Identify and map flash without programming it |
| `w` | Erase and program the dedicated scratch sector; destructive test |
| `d` | Exercise watchdog kicks and pause, then permit a watchdog reset |

The loader requires passing memory, interrupt-mask and register checks,
a ping reply, advancing heartbeats and advancing interrupt-driven ticks.
A failed check, exception, stalled counter or apparent reset fails the run.
`--reopen N` repeats the checks after N console close/open cycles without
reloading RAM. It checks that the timer counters continue across sessions.
The loader leaves the diagnostic running in RAM. A board reset or
power cycle returns to the existing flash firmware; loading another RAM
image also recovers from the terminal trap. No command writes eFuses.

Use DTR and RTS asserted together when opening a terminal. On native USB,
deasserting DTR while RTS remains asserted can reset the chip. The loader's
console helper keeps both asserted on open; the ROM-loader phase changes
them explicitly to request a reset. Software reopen checks do not establish
behavior during a physical cable removal or a power-state transition.

## Offline checks

```sh
python3 tools/esp32c5_ram.py --check build/esp32c5/diagnostic.bin
make -C TikuBench/tests/host/kernel esp32c5-boot
```

TikuBench tests image validation, port selection, USB FIFO behavior,
watchdog register sequences, interrupt ownership, delayed ticks, counter
wrap, bounded snapshot timeouts and linker bounds. A cross-compiled trap
check verifies register slots, alignment and CSR restore order. Mutation
fixtures omit a register restore, corrupt a route, lose delayed ticks,
extend a hardware wait, remove a USB packet bound or change a watchdog key;
the corresponding tests must reject each fixture. These tests do not open a port.
Kernel qualification uses `TikuBench/tools/esp32c5_firmware_check.py`,
which requires an explicit port, log destination and complete expected
test groups. It uses the canonical firmware-marker parser and refuses
failures, skips, missing groups, malformed summaries and terminal faults.
`TikuBench/tools/esp32c5_shell_check.py` runs the file-store and BASIC
round trips on an already running shell. It reserves absent scratch
filenames before writes and removes its test files afterward.
The regular runner and both Bench frontends expose an `esp32c5-usb` profile.
Its kernel selection is limited to arena, pool, region, timer, clock-edge and
watchdog-edge. Filestore exposes the five non-destructive categories, including
BASIC round trips. Destructive layout/region selections and C61 radio modes
are refused. See `TikuBench/docs/RUNNER.md` for explicit-port commands.

`TikuBench/tools/esp32c5_xip_check.py` verifies the installed image pair, loads
a mismatched expectation into SRAM, requires an early halt, and restores flash
boot. It does not write flash. `esp32c5_watchdog_check.py` verifies the installed
firmware, loads the diagnostic, exercises kick/pause/timeout and checks the ROM
watchdog reset cause. The system reset retains ROM-download boot selection;
the helper then requests a USB hard reset to restore flash boot. This is not
a cold-boot watchdog recovery or physical power-cycle test.

The host target also checks CPU divider changes and rollback, reset-cause
translation, profile gating, companion integrity, installation ordering and
SRAM placement. ASan/UBSan and deliberately broken fixtures cover the validators.

## Reference

Register and ROM bindings follow ESP-IDF commit
`4d59230ddff16327812782151ef0afef202dc6d7`:

- `components/esp_rom/esp32c5/ld/esp32c5.rom.ld` and
  `esp32c5.rom.version.ld`.
- `components/soc/esp32c5/register/soc/`: `reg_base.h`,
  `usb_serial_jtag_reg.h`, `timer_group_reg.h`, `lp_wdt_reg.h`,
  `pcr_reg.h`, `systimer_reg.h` and `interrupt_matrix_reg.h`.
- `components/soc/esp32c5/include/soc/`: `clic_reg.h` and `interrupts.h`.
- `components/esp_hw_support/port/esp32c5/systimer.c`: crystal-dependent
  division produces the same 16 MHz timebase with 40 or 48 MHz crystals.
- `components/esp_hal_wdt/esp32c5/include/hal/`: `lpwdt_ll.h` and
  `mwdt_ll.h`.

Interrupts resume the selected kernel or worker frame. Exceptions report
their cause directly through the USB FIFO and halt; they do not provide
kernel fault recovery. The remaining full-port work includes external
memory, SPI/ADC/1-Wire, deep sleep, radios, SDR and general Bench/desktop
profiles. Hardware qualification and outstanding fixtures are recorded in
`kintsugi/esp32c5-port-plan-v2.md` in workspaces that contain that local plan.
