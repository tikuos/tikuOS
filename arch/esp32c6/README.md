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

This profile provides the cooperative core and SDIO diagnostic application.
It rejects enabled shell, BASIC and worker build flags. Its worker and radio
reservations do not enable those subsystems. Native code, handlers and DMA
stages reside in SRAM. During installation the native kernel is stopped;
the preserved SDK recovery and factory applications perform flash operations
using their own flash/cache handling. No native XIP mapping is configured.

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
| File-store candidate | `[0x331000,0x3fc000)` | Unclaimed; preserve contents |
| Durable-mirror candidate | `[0x3fc000,0x400000)` | Unclaimed; preserve contents |

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

## Checks

```sh
make -C TikuBench/tests/host/kernel ipc ipc-transports c6-sdio p4-sdmmc
python3 TikuBench/tests/host/kernel/test_esp32c6_resident.py
python3 -B TikuBench/tests/host/kernel/test_esp32c6_firmware.py
python3 tools/esp32c6_sdio_qualify.py --port /dev/serial/by-id/your-p4 \
    --output /path/to/new-capture --recovery
cd TikuBench
python -m tikubench run ipc-sdio --board firebeetle2_p4 --skip-build --port /dev/serial/by-id/your-p4
```

The qualification tool generates independent source CRCs and checks every
stream's control bound and returned backing. Captures, measured rates and
board-specific qualification records belong in `kintsugi/` or `temp/`.
