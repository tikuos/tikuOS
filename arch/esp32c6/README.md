# ESP32-C6 native kernel

`MCU=esp32c6` builds a native RV32IMAC kernel for the C6 attached to the
FireBeetle 2 ESP32-P4. The resident profile runs in a 64 KiB SRAM interval
under the board's installed resident loader. Its portable process, timer,
scheduler, memory and IPC code is shared with the other ports.

The profile provides copied messages and two 512-byte managed buffers,
framed SDIO packets, the preserved shared-register diagnostic console and
a USB Serial/JTAG console HAL. Its application consumes generated data,
640 × 960 grayscale images and 16-byte sensor records through `c6/bulk`.
The diagnostic application can be replaced with another wire sink. The
profile has no VFS, durable-memory tier, threads or radio driver. Its USB
console needs a connection to the C6's USB signals; the FireBeetle's P4 USB
connector carries the SDIO diagnostic console.

## Native flash entry

`C6_PROFILE=cold` builds a native SRAM application entered by the board's
installed ESP application bootloader. It initializes traps, timer and
watchdog ownership, a wrapping CPU cycle counter and the SDIO DMA master's
trusted access to SRAM. The native path starts TikuOS without entering the
SDK application or issuing a resident GO. The bootloader runs at 80 MHz;
the resident profile inherits the loader's CPU frequency.

The application also preserves the installed `tiku-c6-access-1` recovery
segments and their flash offsets. C6 GPIO2 samples the board WAKE wire at
entry: low selects TikuOS, high selects the preserved SDK recovery entry.
P4 GPIO6 drives WAKE and GPIO54 drives C6 EN. These are board-reserved
signals. Recovery selection remains available when the native kernel fails
before SDIO initialization. This route requires a valid combined app and
bootloader; GPIO9 ROM-download selection remains a separate physical route.

Build the native image and compose it with a preserved recovery application:

```sh
make MCU=esp32c6 C6_PROFILE=cold cold-image \
    C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
```

Outputs reside in `build/esp32c6/cold`. `main.bin` is the native SRAM
payload and `app.bin` is the ESP application-slot image. The composer checks
the native ELF, recovery entry, segment bounds, checksum and SHA-256. It
retains recovery segment bytes and positions, and refuses overlaps with
the native 64 KiB arena. The upper 4 KiB are reserved for the entry caller
stack and bounded early-stage/trap observations. That SRAM record has no
reset-retention guarantee. Report words 58..61 expose the ROM reset reason,
`0x434f4c44` cold-entry identity, inherited HP access filters and inherited
SDIO security mode.

The cold profile's `flash` target refuses resident-bank installation.
`app.bin` belongs in a compatible application slot, with the factory image,
boot prefix and resident banks preserved. Its creation does not modify a
board. The recovery image must match the board's partition geometry and the
compiled recovery entry, default `0x40800896`; another entry requires
`C6_SDK_ENTRY` and a matching validated recovery image.

Build the matching P4 firmware with
`TIKU_IPC_ENABLE=1 TIKU_SDIO_C6_ENABLE=1 TIKU_C6_COLD_BOOT_ENABLE=1`.
Its first SDIO service resets C6 into native entry. A native startup failure
selects recovery-high and performs a bounded loader transition. A failed
recovery attempt remains failed; the service does not retry indefinitely.
The explicit `ipc sdio native` command resets into native flash entry.
`ipc sdio loader` selects SDK recovery. The P4 cold-boot option requires an
installed WAKE-selectable application; use the resident option for boards
whose installed image requires selected-bank GO.

```sh
python3 tools/esp32c6_sdio_qualify.py --port /dev/serial/by-id/your-p4 \
    --output /path/to/new-capture --cold-boot --warm-resets 10 \
    --soak 65 --profiles image --recovery
```

Warm-reset evidence uses EN and the existing controller owner. Cold-power
qualification requires removing board power and repeating the heartbeat,
self-test and P4-console checks after reconnection.

## Firmware memory and downstream installation

`C6_PROFILE=firmware` runs the native core in the 400 KiB SRAM interval
`[0x4080a000,0x4086e000)`. The lower 40 KiB remain available to the preserved
SDK recovery image. The upper SRAM beyond the native arena is excluded for
the bootloader and ROM. LP SRAM is excluded. P4 PSRAM is peer storage
reachable through IPC; the C6 cannot address it as local memory.

The linker assigns initialized code/data and static storage first, followed
by the SRAM allocator tier and these fixed reservations:

| Reservation | Bytes |
|-------------|------:|
| Worker stacks | 32,768 |
| Radio storage | 65,536 |
| IRQ stack | 2,048 |
| Main stack and guard | 8,256 |
| Cold-entry caller stack and fault record | 4,096 |

The initialized image budget is 131,072 bytes. `C6_TIER_MIN` sets the
allocator floor, default 196,608 bytes. Linker assertions reject a footprint
below that floor or beyond the arena. `firmware.json` records the ELF and
image hashes, build identity and SRAM spans; the package validator checks
the ELF's allocated sections, reservations and initialized bytes. Startup
tests the free tier with two address-dependent patterns before admitting it
to the allocator. `memtest`, `memsize`, `tiersize`, `workers` and `radio`
are native diagnostic commands through `ipc sdio console`.

This profile provides the cooperative core, worker backend and SDIO diagnostic
application. `TIKU_SHELL_ENABLE=1 C6_STORAGE=1` adds the remote shell and VFS;
`TIKU_SHELL_BASIC_ENABLE=1` adds BASIC. The radio reservation does not enable
a radio driver. The default profile keeps core code, handlers and DMA stages in SRAM. Shell
and interpreter code use a separate flash mapping. During installation the native kernel is stopped;
the preserved SDK recovery and factory applications perform flash operations
using their own flash/cache handling. `C6_STORAGE=0` leaves native flash
mapping and durable storage disabled.

Build the package with a preserved recovery image from the target board:

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_BUILD=1 firmware-image \
    C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
```

Outputs in `build/esp32c6/firmware` are `main.bin`, `main.elf`, `app.bin`
and `firmware.json`. Keep the ELF, application and JSON report together.
Install through the P4 with its matching shell/SDIO firmware and an idle USB
console; the Python interpreter needs pyserial:

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_BUILD=1 flash \
    C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin \
    C6_P4_PORT=/dev/serial/by-id/your-p4 C6_P4_SERIAL=your-usb-serial \
    C6_PEER_MAC=aa:bb:cc:dd:ee:ff C6_PYTHON=/path/to/python-with-pyserial \
    C6_UPDATE_LOG_DIR=/path/to/new-capture
```

The installer requires the FireBeetle C6 revision 0.2, 4 MiB flash,
unencrypted/unsigned operation, the existing partition table and a valid
MAC-bound resident ownership record. It changes no security fuses. The
versioned flash map retains these regions:

| Region | Interval | Installer policy |
|--------|----------|------------------|
| Bootloader and partition table | `[0,0x9000)` | Preserve |
| NVS | `[0x9000,0xd000)` | No installer writes; SDK calibration can update it |
| OTA selection | `[0xd000,0xf000)` | SDK selects application |
| PHY | `[0xf000,0x10000)` | Preserve |
| Factory application | `[0x10000,0x190000)` | Preserve |
| Native/recovery application | `[0x190000,0x310000)` | Update |
| Resident owner and banks | `[0x310000,0x331000)` | Preserve |
| NVM region | `[0x331000,0x3fc000)` | Preserve; native storage uses it with `C6_STORAGE=1` |
| Durable mirrors | `[0x3fc000,0x400000)` | Preserve; native storage uses them with `C6_STORAGE=1` |

The installer does not migrate partitions or format storage. It saves the
previous application and a flushed host transaction journal before selecting
the factory fallback. Factory identity is checked before OTA begins. The
new application's recovery prefix must match the installed recovery bytes.
After OTA, readback checks the installed image and protected regions before
the recovery application marks the image kept and starts the native core.
Native self-test, build identity, memory test and budgets must pass.

Use `tools/esp32c6_install.py` directly with the same image, ELF and metadata
to check the target without updating it, or resume an interrupted transaction:

```sh
python3 -B tools/esp32c6_install.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --peer-mac aa:bb:cc:dd:ee:ff \
    --image build/esp32c6/firmware/app.bin \
    --elf build/esp32c6/firmware/main.elf \
    --metadata build/esp32c6/firmware/firmware.json \
    --output /path/to/new-capture --check-only
```

Replace `--check-only` with `--resume` and supply the interrupted capture
directory to continue. Resume checks the recorded identities, image and
previous-application backup. It verifies an already installed image or
restarts the factory update. The journal and backup must remain together.
`ipc sdio factory` opens the owned factory transport; `ipc sdio attach`
adopts the SDK recovery transport after factory OTA without another EN reset.
The updater waits for the factory's delayed restart before attachment.

The installer's `--interrupt before-erase`, `--interrupt payload` and
`--interrupt selection` options exercise reset/retry paths. The last resets
after SDIO transmission of OTA_END completes, before accepting its reply;
it does not select an internal flash-instruction boundary. These are EN
reset tests, with the factory and bootloader preserved. Physical power cuts
during a flash operation require separate qualification.

## Preemptive workers

The firmware profile defaults to `TIKU_THREADS_ENABLE=1`. It uses the shared
`kernel/threads/tiku_thread.c` policy: cooperative kernel work has priority;
ready workers run round-robin when the kernel has no pending process or IPC
work. Each timer interrupt wakes the kernel, including when no software timer
is armed. A worker can compute, post events and use the worker wait API.
Allocator mutators retain their worker-context refusal checks.

The C6 backend routes CPU software interrupt zero (source 22) to CPU line
18; the timer uses line 17. The software request is acknowledged before the
shared scheduler chooses the return frame. Interrupts are masked throughout
dispatch on the dedicated 2 KiB IRQ stack. Context frames preserve the RV32
integer registers, `mepc` and `mstatus`; entry frames select machine mode
with interrupts enabled on exception return. This RV32IMAC profile has no
floating-point context. The core links freestanding routines. The shell
profile links newlib-nano and libm; `_sbrk` refuses libc heap allocation.

`TIKU_THREAD(name, bytes)` places C6 worker stacks in the linker's 32 KiB
worker reservation through `TIKU_THREAD_STACK_ATTR`, with 16-byte alignment.
Their combined size must fit that reservation. The shared API requires at
least 512 bytes per stack. Canary checks detect a changed bottom word at a
switch; the port provides no worker stack memory-protection boundary.

The default diagnostic application starts no workers. Applications use
`tiku_thread_start()` and the shared wait, wake, budget, join and forget APIs.
`TIKU_THREADS_ENABLE=0` builds the firmware core without workers. Resident
and cold profiles require workers disabled and retain their SRAM limits.

Build the worker qualification application with:

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_THREAD_TEST=1 C6_BUILD=42 \
    firmware-image C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
```

Install that package with the checked installer. The qualification profile
runs startup checks for lifecycle, capacity, timer preemption, register
preservation, nested atomic sections, kernel priority, waits, cycle budgets,
stack canaries and allocator confinement. Its startup wait test sets the
tick counter near wrap before admitting diagnostic/IPC service. The test
records one intentional canary fault and one refused worker allocation.
Those hooks and the tick adjustment are absent when `C6_THREAD_TEST=0`.

```sh
python3 -B tools/esp32c6_threads_qualify.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --build 42 --output /path/to/new-capture --seconds 3600
```

The host checks the startup result, starts three non-yielding workers with
assembly register sentinels, and transfers repeated image/sensor/slow/corrupt
objects while checking timer progress and unchanged guard counts. The result
distinguishes a shorter diagnostic run from the one-hour qualification.
`thrtest`, `thrsoak` and `thr 0` through `thr 9` expose this test profile's
failure bitmap, soak start and observations; they are absent from the
ordinary diagnostic application. Restore an ordinary package after testing.

## Build and update

Build with the xPack RISC-V toolchain named by `TOOLCHAIN_DIR` and
`TOOLCHAIN_PREFIX`:

```sh
make MCU=esp32c6
```

The output directory defaults to `build/esp32c6/resident`. It contains
`main.elf`, `main.bin` and the 32-byte `manifest.bin`. The image checker
validates the entry point, initialized bytes, SRAM extent, unresolved
symbols and loader limits before creating the manifest. The default arena
base is `0x4080a000`; `C6_RAM_BASE` must match the live loader's INFO reply.
The initialized image is limited to 49,152 bytes, and runtime storage,
including a 4 KiB SRAM tier, 2 KiB interrupt stack and 8 KiB main stack,
must fit the 64 KiB interval.

The P4 firmware needs `TIKU_IPC_ENABLE=1 TIKU_SDIO_C6_ENABLE=1` and the
shell. Install it with the P4 port's checked firmware tool. The C6 update
uses the installed resident loader, without an external USB-to-UART bridge:

```sh
make MCU=esp32c6 flash C6_P4_PORT=/dev/serial/by-id/your-p4 \
    C6_PYTHON=/path/to/python-with-pyserial
```

`C6_BANK` selects bank 0 or 1, default 0. The uploader checks the live
arena and the other bank's valid manifest before writing. It verifies the
committed image, the unchanged other bank and the boot-prefix/factory
checksums, then explicitly runs the selected image and checks the native
self-test. The update cannot install a resident loader on an unprepared
board. `C6_UPDATE_LOG_DIR` selects a new capture directory.

## Native SDIO

P4 owns hardware SDMMC slot 1 and the wired C6 enable signal. Its control
queue, two bulk jobs and loader job share one controller lease. Native
control retains the 12/24-byte payloads and sequence/session/CRC envelope.
Bulk packets use separate fixed stages and descriptors. Engine-visible
storage is internal SRAM; private application loans are copied into those
stages. An unsuccessful abort retains physical staging.

| Access domain | Permitted storage | Peer / engine | Alignment / cache line |
|---------------|-------------------|---------------|------------------------|
| P4 SDMMC | Linker-owned internal SRAM device carve at `0x4ff40000`, excluded from the general tier | 1 / 1 | 64 / 64 bytes |
| P4 packet copy | The same owned carve, copied into the controller's separate stages | 1 / 0 | 64 / 64 bytes |
| C6 SDIO | Fixed stages and descriptors inside the 64 KiB resident SRAM arena | 1 / 1 | 64 bytes / uncached |

The domains admit input and output. General SRAM allocations, PSRAM and
caller stack spans are not imported into either DMA engine. Application
buffers reach physical stages through bounded CPU copies.

The host commits a fresh 64-bit nonce in a twelve-byte `IPCE` payload
inside the validated native control envelope. The C6 arms packet DMA
after that commit and acknowledges after its endpoint binding and armed
packet are available following scheduler service. P4 waits for a receive
credit and the first complete packet, then verifies the resident identity
through a second control
exchange before publishing native-ready. Wire negotiation confirms both
epochs before admitting payloads. The P4 can quiesce and reset into the
loader without a native acknowledgement.
The resident profile returns to native mode through selected-bank GO.
After GO the host starts enumeration with CMD0, then CMD5/3/7 and function
configuration. The target peripheral reset invalidates the loader's bus
state even when the CCCR version remains readable.

Each target exchange uses one descriptor per direction. Send completion
requires SLC RX_EOF with descriptor ownership returned; receive completion
requires SLC TX_DONE and returned ownership. The send FIFO is reset before
each new packet, and completion bits are cleared before arming. An idle
offered packet remains armed until a transfer half completes.

On the P4 console:

```text
ipc sdio probe
ipc sdio run 0
ipc sdio qualify
ipc sdio console "echo hello"
ipc sdio image
ipc sdio sensor
ipc sdio slow
ipc sdio corrupt
ipc sdio interrupt
ipc sdio loader
ipc uart
```

Image, sensor and generated-byte profiles transfer 600 KiB through a
window of two chunks. The slow consumer returns `AGAIN` between accepts;
the corruption profile changes every hundredth accepted INPUT frame.
`interrupt` resets while the local descriptor engine owns a stage. `uart`
uses UART0 hardware loopback independently of the USB console.

The native `buskhz` diagnostic borrows idle PCNT0 to count the wired SDIO
clock for 1 ms, using the 16 MHz SYSTIMER reference. `worklo`/`workhi`
count CPU cycles inside scheduler/IPC service; stack diagnostics report
painted headroom above their guards.

`ipc sdio pause` from loader-ready holds packet pumping across the next
native boot for diagnostic control. `ipc sdio resume` enables packets.
Native `dma 0` through `dma 5` report engine flags, raw interrupt state,
receive/send descriptor controls and receive/send link state. Recovery
observations capture the last request marker, ready marker, credits and
offered-byte register before a reset.
`dma 0` packs active/receive-done/send-done into bits 0..2, the low 16 bits
of SLC RX status into bits 8..23, and receive/send link park flags into
bits 24..25. The unused IPCE reply padding carries startup observations
until another diagnostic reply replaces it.
P4 recovery interprets those observations only when the captured envelope
has a valid IPCE reply; another diagnostic response has no startup trace.

## Durable storage

`C6_STORAGE=1` enables native storage in the firmware profile. It defaults
to zero. The installed 4 MiB geometry, ROM chip record and resident ownership
record must match the port's flash map before writes are admitted. Native
code and flash staging remain in SRAM. Reads use the mapping at `0x42000000`;
SPI1 programs pages of at most 256 bytes and erases 4 KiB sectors with
interrupts masked and the cache suspended. Readback verifies each operation.
An I/O failure disables further flash writes until reset. ISR calls,
non-SRAM program sources and writes below `0x331000` are refused.

The NVM region has 812 KiB. The shared layout service assigns its first
32 KiB to the NVM allocator by default and derives file-store capacity from
the remaining extent: 192 files and 193 data slots with this split. A slot
occupies one 4 KiB sector and carries 4092 payload bytes; files may span
contiguous slots. Mount does not format. Blank-region provisioning and
foreign or malformed ownership follow the shared layout contract.

Two alternating 8 KiB mirrors store the durable SRAM image. Each has a
16-byte CRC, size, generation and magic header; magic is programmed last.
The live image is limited to 8176 bytes, including the layout prefix and a
4112-byte sector undo record at image offset 1024. The linker and package
validator enforce these bounds. The remaining durable capacity depends on
the linked persist-cell registry and application objects.

Region writes preserve bytes outside their range. Before a sector erase
or aligned four-byte update, the backend commits the old sector to the
durable mirror. It clears that undo record durably before returning success.
Startup replays a pending record before cell migration or layout admission.
Multi-sector raw writes can persist a prefix; TFS provides its file commit
contract above the backend. Invalid mirror generations, CRC failures without
a valid alternate, and malformed undo records hold storage.

PMA entries 13 and 14 make the durable SRAM span read-only outside an NVM
write window. An unbracketed store raises a store access fault and enters
the native abort path. Persist-cell commits return the mirror flush status.
Unchanged mirror images and page ranges do not issue program commands.
Changed images erase the alternate mirror; sector undo adds mirror commits.
This backend has no wear leveling. Use SRAM for frequently changing buffers.

The P4 publishes a consumed-packet word in communication register W6 only
after its receive DMA completes. The C6 checks the encoded byte count and
epoch check bits before accepting that acknowledgement as send completion. Both DMA
descriptors must have returned before buffers can be reused. Host interrupt
register W7 is excluded from acknowledgement writes. CONTROL requests use
their admitted deadline; the P4 diagnostic client admits two seconds.
Storage transactions require the matching P4 acknowledgement and control
deadline implementation.

Build and exercise the storage test profile:

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1 C6_STORAGE_TEST=1 \
    C6_BUILD=1 firmware-image C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
python3 -B tools/esp32c6_storage_qualify.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --build 1 --output /path/to/new-capture --allow-fixture-writes
```

The qualifier writes and removes `_m4_packet` and changes its test persist
cell. `--fill-empty-store` requires an empty store and creates, verifies
capacity refusal, and removes `_m4_NNN` fixtures. `--pause-packets` records
storage tests with packet pumping disabled. These are separate from tests
with concurrent packet traffic. EN reset preserves flash supply; physical
power interruption requires a separate fixture or operator test.
Retained writes through the architecture NVM-copy API stay in SRAM; the
port admits the linked durable and retained ranges and refuses other destinations.
Retained SRAM is qualified across ROM software reset within the same
linked image. An EN-driven restart has no SRAM retention guarantee.

`esp32c6_storage_reset_qualify.py` uses the same port, identity, build,
output and fixture-write arguments. It requires a P4 built with
`EXTRA_CFLAGS=-DTIKU_SHELL_CMD_DELAY=1`. It checks software-reset retention
and interrupts cell/file commits after selected completed flash operations.
The P4 delay prevents its automatic recovery from changing C6 reset cause
before the native boot records it. Byte-level interruption coverage is in
the host model; this board test does not interrupt a flash instruction or
remove flash power.

## Remote shell, VFS and BASIC

The firmware shell mounts `/sys`, `/dev`, `/proc` and `/data` through the
boot provider table. The RAM console backend reaches C6 through P4's
`ipc sdio shell` bridge, USB frame channel `0xf6` and the existing SDIO
control owner. Board checks cover shell commands, namespace providers,
memory reports, file storage, BASIC execution and software reboot. A C6 GPIO9-to-GND contact is required for
ROM recovery if a combined image cannot enter either application path.

```sh
make MCU=esp32c6 C6_PROFILE=firmware C6_STORAGE=1 \
    TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1 C6_BUILD=1 \
    firmware-image C6_RECOVERY_IMAGE=/path/to/tiku_c6_access.bin
python3 -B tools/esp32c6_shell.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --peer-mac aa:bb:cc:dd:ee:ff --output /path/to/new-capture \
    --command 'ls /' --command 'free' --command 'ps'
```

Install the matching P4 firmware with the shell, IPC console, IPC and SDIO
C6 owner enabled. The downstream tool checks the C6 factory MAC and prints
its build identity. A session carries a 32-bit identifier and nonwrapping
16-bit request sequence. Exact retries return the cached response. Input
has backpressure; output loss is an explicit error. The input/output rings
hold 127/4095 bytes. A one-byte zero frame closes the P4 bridge. Idle sessions expire after 30 seconds; close or expiry
cancels a partial shell command. Open a new session before 65,535 requests.
The remote backend grants the same shell capabilities as the local board
console. It provides identity selection, without an authentication layer.

Flash operations service console control after restoring cache and interrupt
state. This path copies console bytes through RAM queues and refuses other
diagnostics while a flash operation is in progress. It does not enter the
shell, interpreter or filesystem recursively.

The shell's default SRAM tier floor is 65,536 bytes; the core retains its
196,608-byte floor. The shell retained-state budget is 8192 bytes. BASIC
has 100 program lines. `/sys/mem/sram_map` reports the linked reservations.
The package records the flash section's bytes, address and hash. Its address
is `0x42210000`, at OTA1 file offset `0x80000`. An ignored ESP image segment
carries those bytes; the recovery image's two mapped segments retain their
positions. The native flash driver installs the mapping before shell entry. The shell
profile refuses startup if native flash or durable-state admission fails;
the P4 can then select the preserved recovery application.

The P4 object-stream diagnostics register the remote channel while the bulk
stream advances. A bulk exchange starts between successive remote requests.
The console qualifier checks malformed frames and exact retries, command
responses during a 600 KiB object, independent payload CRCs, returned buffers
and the 256-tick control bound. The slowed sensor stream with remote commands
is board tested. It records the command interval and measured
latencies in its result file.

```sh
python3 -B tools/esp32c6_console_qualify.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --peer-mac aa:bb:cc:dd:ee:ff --profile slow --interval 2 \
    --output /path/to/new-capture
```

## Peripheral capabilities

The native HAL sources are part of the shell firmware profile. Register
models check their programming sequences; they do not qualify electrical
behavior. The table distinguishes those checks from board measurements.
Strap reservations follow the [C6 datasheet](https://documentation.espressif.com/esp32-c6_datasheet_en.html).
Chip block presence is recorded against ESP-IDF commit
`4d59230ddff16327812782151ef0afef202dc6d7` and its C6 SOC capability headers.

| Block | Native implementation and verification | FireBeetle C6 wiring / remaining qualification |
|-------|-----------------------------------------|-----------------------------------------------|
| SDIO slave | Controller ownership, copied IPC messages and bulk streams; board tested | Wired to P4 GPIOs; reserved from application GPIO |
| SYSTIMER tick and CPU interrupts | Native tick, traps and workers; board tested | Internal; target0 count rate and interrupt callback board tested |
| GPIO and edge IRQ | HAL and pin ownership; reserved-pad refusal board tested; register models | C6 signals are not exposed on P4 headers; edge fixture pending |
| UART0 | 8N1, bounded TX, interrupt RX, internal loopback; register models | GPIO16/17 reserved; 256-byte internal loopback with IRQ receive board tested; external-pad tests require a fixture |
| I2C0 | 100/400 kHz, repeated START, ACK/NACK, bounded errors; register models | Default pins255 refuse init; pads, pull-ups and target fixture required |
| SPI2 | Polled master, modes0..3, bit order, bounded transfers; register models | Default pins255 refuse init; pads and peer fixture required |
| ADC1 | Raw one-shot GPIO0..6, ground trim, state restoration; register models | `C6_ADC_PIN_MASK=0` refuses external channels; voltage fixture pending |
| Watchdog | TG0 native boot watchdog board tested; HAL configure/pause/resume register models | Internal; HAL-triggered TG0 reset and SDIO recovery board tested |
| Entropy | Internal SAR completion checks and hardware RNG; native storage UUID path board tested | Internal; active ADC, temperature or continuous conversion refuses acquisition |
| CPU clock | Reports ROM clock; fixed inherited rate, 80 MHz on the native cold path | Clock changes are unavailable through this profile |
| Idle and wake | WFI hook, armed-source query; query register models | Tick, alarm, GPIO and UART query; measured WFI entries and SDIO shell continuity board tested |
| LEDC / MCPWM | Present in silicon; C6 driver absent | PWM pins require module-pad access; no shared PWM HAL is implemented |
| I2S / TWAI / RMT / PCNT / PARLIO | Present in silicon; general C6 drivers absent | PCNT0 is borrowed by the SDIO clock diagnostic; application fixtures pending |
| Wi-Fi | Opt-in native station adapter; open/WPA2, DHCP, UDP/TCP and reconnect board tested | See [C6 driver](../../drivers/wifi/esp/c6/README.md) for qualification limits |
| P4 Wi-Fi service | Exclusive station claim, capability discovery and Ethernet frames over the native mailbox | The [IPC station profile](../../drivers/wifi/ipc/README.md) runs IP on P4 and excludes local C6 IP ownership |
| BLE / IEEE 802.15.4 | Present in silicon; native drivers absent | Antenna on C6 module |
| SDR | Native driver absent; silicon support unqualified | No SDR qualification for C6 |
| PSRAM / deep sleep / USB OTG | PSRAM and deep-sleep integration absent; USB OTG unsupported by C6 silicon | C6 has no local PSRAM; USB Serial/JTAG signals are not on P4 USB connector |

Raw GPIO uses one-based eight-pin ports, GPIO0..30. GPIO2 is WAKE; GPIO4/5/8/9/15
are straps; GPIO12/13 are USB Serial/JTAG; GPIO16/17 are UART0; GPIO18..23
are SDIO; GPIO24..30 are flash. Mutation of these pads is refused. GPIO4..7
include JTAG signals; GPIO6/7 refuse mutation while that mux remains active. Peripheral input
and output routes and software pin claims prevent application reassignment.
The P4 board's headers expose P4 GPIOs; their numbers do not name C6 pins.

SPI and I2C require board-selected `TIKU_BOARD_SPI_*_PIN` and
`TIKU_BOARD_I2C_*_PIN` constants. ADC reference names select attenuation,
not calibrated voltages: readings are raw 8/10/12-bit counts. Internal
ADC channels30/31 are unavailable through this driver. ADC transactions
refuse access while the modem front end is clocked. Entropy reads require
foreground context. They mask interrupts, use calibrated RF noise while
the PHY owns the analog bus, and acquire the idle SAR otherwise. PHY
transitions refuse reads. SAR reads wait for new reserved-channel conversions
and restore hardware. Errors clear the destination. Wi-Fi startup is not
required for SAR entropy.

WFI keeps the peripheral domains and SDIO ownership active. Deep sleep and
power-domain shutdown have no native resume implementation. External I2C,
SPI, ADC and GPIO tests require access to C6 module signals. Host models
and UART internal loopback do not establish those paths.

The internal qualification profile enables `C6_PERIPHERAL_TEST=1` with the
shell and storage. Build the C6 with `TIKU_SHELL_CMD_HTIMER=1` and
`TIKU_SHELL_CMD_SLEEP=1` in `EXTRA_CFLAGS`, and build P4 with
`TIKU_SHELL_CMD_DELAY=1`. The test uses UART hardware loopback, refuses
protected GPIO claims, samples entropy, measures the alarm, checks actual
WFI entries and deliberately stops feeding TG0. A durable test cell records
the ROM reset reason before the P4 recovery path changes it. The ordinary
firmware omits these test hooks.

```sh
python3 -B tools/esp32c6_peripheral_qualify.py \
    --port /dev/serial/by-id/your-p4 --p4-serial your-usb-serial \
    --peer-mac aa:bb:cc:dd:ee:ff --build 42 --output /path/to/new-capture
```

## Checks

```sh
make -C TikuBench/tests/host/kernel ipc ipc-transports c6-sdio p4-sdmmc
make -C TikuBench/tests/host/kernel c6-threads
make -C TikuBench/tests/host/kernel c6-storage console-remote c6-peripherals
python3 -B TikuBench/tests/host/kernel/test_esp32c6_resident.py
python3 -B TikuBench/tests/host/kernel/test_esp32c6_firmware.py
python3 tools/esp32c6_sdio_qualify.py --port /dev/serial/by-id/your-p4 \
    --output /path/to/new-capture --recovery
cd TikuBench
python -m tikubench run ipc-sdio --board firebeetle2_p4 --skip-build --port /dev/serial/by-id/your-p4
```

The qualification tool generates independent source CRCs and checks every
stream's control bound and returned backing. Captures, measured rates and
board-specific qualification records belong in `kintsugi/` or `temp/`.
