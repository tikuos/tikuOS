# ESP32-C6 resident kernel

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
Returning to native mode requires an explicit selected-bank GO.
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
python3 tools/esp32c6_sdio_qualify.py --port /dev/serial/by-id/your-p4 \
    --output /path/to/new-capture --recovery
cd TikuBench
python -m tikubench run ipc-sdio --board firebeetle2_p4 --skip-build --port /dev/serial/by-id/your-p4
```

The qualification tool generates independent source CRCs and checks every
stream's control bound and returned backing. Captures, measured rates and
board-specific qualification records belong in `kintsugi/` or `temp/`.
