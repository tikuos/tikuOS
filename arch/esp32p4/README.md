# ESP32-P4 native firmware and qualification profiles

## Normal firmware

`P4_PROFILE=firmware` builds native CPU0 TikuOS with the shell, VFS, BASIC,
integer workers, flash-backed `/data`, durable mirrors, clock preferences,
paired flash execution and automatic attachment of the fitted 32 MiB PSRAM.
It supports the Olimex ESP32-P4-PC and the DFRobot FireBeetle 2 ESP32-P4
(`BOARD=firebeetle2_esp32p4`, below), both with silicon v1.3 and ROM ECO2.
CPU1 remains parked, and so does the LP core unless the build links its
payload (`TIKU_LPCORE_ENABLE=1`, below). No FreeRTOS or ESP-IDF runtime is
linked.

```sh
make MCU=esp32p4 P4_PROFILE=firmware \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESPTOOL=/path/to/venv/bin/esptool
```

Artifacts are `build/esp32p4/firmware/main.bin`, `main.paired.elf` and
`xip.bin`. The unpaired `main.elf` is for symbols and debugging. The loader
requires the paired ELF. Shell, BASIC, application and kit code, the VFS
tree's node handlers and tables, their constants, and driver functions
marked `TIKU_P4_XIP_TEXT` (the microphone's, the ISP's, the JPEG
codec's, the PPA's and the 2D-DMA's, apart from what their interrupt
paths call) execute from flash. The
rest of the kernel, cache/flash controller operations and their dependencies
execute from internal SRAM; a flash operation runs there with interrupts
masked, so no flash-resident code runs during one. The profile uses a 16 KiB
foreground stack and a separate 2 KiB interrupt stack. PMP entry 0, locked at
boot, faults any access below 4 KB; PMA entries 0 and 1 hold the durable span
(`.persistent`, the SRAM working copy of the flash mirror) read-only outside a
`tiku_mpu_unlock_nvm()` window. A store there outside a window is a store
access fault that the trap entry latches (a flag, a count and the address,
read through `tiku_mpu_get_violation_flags()`, `_count()` and
`_last_fault_addr()` or `/sys/boot/mpu/{violations,count,last_addr}`) and
skips, so the kernel keeps running; every other
exception halts with the retained fault record. `diag pmp`, `diag pma` and
`diag fault durable` show the entries and the latch.

### Install and recover

On the Olimex, ordinary `make flash` and `make erase` refuse this profile.
Installation
changes the original application's layout and can enable storage writes
at boot; it requires the storage-aware installer, not the diagnostic loader.
Keep an immutable full 16 MiB original backup and a non-writing kernel-test
image with its ELF. The first install requires the flash to match that backup
and the entire native storage reservation to be erased and outside original
partitions. An unknown existing store is not reformatted.

```sh
python tools/esp32p4_firmware.py \
    --image build/esp32p4/firmware/main.bin \
    --elf build/esp32p4/firmware/main.paired.elf \
    --xip build/esp32p4/firmware/xip.bin --check-only

python tools/esp32p4_firmware.py \
    --image build/esp32p4/firmware/main.bin \
    --elf build/esp32p4/firmware/main.paired.elf \
    --xip build/esp32p4/firmware/xip.bin \
    --first-install --backup /path/to/original-16MiB.bin \
    --recovery-image build/esp32p4/kernel-test/diagnostic.bin \
    --recovery-elf build/esp32p4/kernel-test/diagnostic.elf \
    --port /dev/serial/by-id/USB_DEVICE --serial EXACT_USB_SERIAL \
    --log /path/to/new-install.log
```

Build the recovery pair with `P4_PROFILE=kernel-test`, using the same
toolchain options. All hardware tools require pyserial and esptool 5.1.0.

The installer verifies the companion before the boot image, checks the
whole-chip result, and requires a shell prompt. Failed first installs attempt
restoration of changed code/storage sectors and load the recovery diagnostic.
`--restore-original` replaces `--first-install` to request that restoration
explicitly; it removes native files and settings, restoring the original
16 MiB contents, then RAM-loads the diagnostic. A change outside the declared
code/storage reservations stops restoration. Preserve the backup and log.

If BOOT was sampled low, normal flash startup is refused before any write.
An explicit `--ram-run` alongside `--first-install` installs the pair and
starts it through the ROM RAM loader. It is not evidence of flash boot.
With the matching pair already installed, `--resume` replaces
`--first-install` and verifies the code before resetting/RAM-loading it;
the loader makes no flash writes. Firmware itself can update boot counters
and other durable state. This path preserves existing `/data` for warm-reset
tests. It is not an updater for a different firmware pair.

### FireBeetle 2 ESP32-P4

`BOARD=firebeetle2_esp32p4` builds the same firmware for the DFRobot
FireBeetle 2 ESP32-P4 (DFR1237): v1.3/ECO2 silicon, the EF:4018 16 MiB
flash and 32 MiB PSRAM, as on the Olimex. No original image is kept on this
board, so `make flash` installs it without a backup. The first install over
the vendor firmware needs one whole-chip erase:

```sh
make erase MCU=esp32p4 P4_PROFILE=firmware BOARD=firebeetle2_esp32p4 \
    ESP_PORT=/dev/cu.usbmodemN
make flash MCU=esp32p4 P4_PROFILE=firmware BOARD=firebeetle2_esp32p4 \
    ESP_PORT=/dev/cu.usbmodemN
```

`tools/esp32p4_board_flash.py` runs the ROM, silicon, security and flash
checks of the installer, refuses an image built for another board and a
flash that still holds an ESP-IDF partition table, then writes and verifies
the code pair, boots it from flash and waits for the shell prompt. It writes
no storage, so `/data` survives a reinstall; `make erase` clears it. The
ROM loader on this board reports the strap of its USB download entry (0x4)
rather than the BOOT pin, so the shell prompt, not the strap, shows that the
board booted from flash. TikuBench builds and flashes it:

```sh
cd TikuBench
python -m tikubench run p4-firmware --board firebeetle2_p4 \
    --port /dev/cu.usbmodemN --flash
```

`boards/tiku_board_firebeetle2_esp32p4.h` records the board's wiring from
the DFR1172 schematic V1.0. GPIO3 drives the green user LED, high for on
(`/dev/led0`); GPIO35 is the BOOT button, read low when pressed with the
pad's pull-up on (`/dev/btn0`). The GPIO API reserves the pads wired to the
ESP32-C6 (6, 14-19, 54), the PDM microphone (9, 12), the USB console
(24, 25), the TF slot and its power switch (39-45), the LED and the button.
It also refuses a pad routed to a peripheral: an IO MUX function other than
GPIO, UART0 on 37/38 or the SD slot 0 bus on 39-48 in function 0, a matrix
output or input signal, or LP IO MUX control while the LP core holds the
pad. GPIO2-5 count as JTAG pads only when eFuse selects pad JTAG; the board
takes JTAG over USB. Every other pad, the header pins among them, accepts
`gpio`, `/dev/gpio` writes and `irq` edges on GPIO_INTR0 (matrix source 74,
CLIC line 3), each edge broadcast as `TIKU_EVENT_GPIO`. While the LP core
drives GPIO3, `/dev/led0` writes do not reach the pad.

The board's MSM261DGT003 PDM microphone takes its clock from GPIO12 and
returns its bit stream on GPIO9, its select pin tied low. `tiku_pdm_arch.c`
runs it through I2S0, the one I2S instance with the PDM-to-PCM filter:
the receive clock comes from the 40 MHz crystal through the fractional
divider in `HP_SYS_CLKRST`, the microphone clock is the sample rate times
the decimation (64 from 15625 Hz up, 128 below, so the clock stays at
1 MHz or more), and the filter's 35.5 Hz high-pass removes the offset.
AHB DMA channel 2 (peripheral 3, CLIC line 12, source 58) fills a ring of
eight 1 KB buffers of 16-bit mono samples, taken from the SRAM tier's high
span at start and given back at stop, refusing a buffer the core has
not handed back, so an overrun stops the engine and is counted; the next
read that finds the ring empty restarts the stream. `tiku_p4_pdm_start()`
takes 8000-39000 Hz and the left, right or both slots,
`tiku_p4_pdm_read()` blocks for up to a given time, and the owner table
reports the two pads as `microphone`: no other driver or the `gpio`
command can take them. The `mic` command starts and stops it, prints the
level of the last buffer, the counters and a run of samples. The board
test `pdm-p4` measures the rate against the system timer; which slot the
microphone's data lands in, and the clock edge the silicon samples it on,
are what the board confirms.

UART0 drives GPIO37 (TX) and GPIO38 (RX), header P1 pins 17 and 18, in
their IO MUX function, 8N1 at 300 to 3000000 baud from the 40 MHz crystal
(`tiku_uart_arch.c`). Nothing starts it at boot: the console stays on USB.
`tiku_uart_init()` starts it on the pads at `TIKU_BOARD_UART_BAUD`, and
`tiku_p4_uart_start()` also offers an internal loopback that leaves GPIO37
an undriven GPIO; `tiku_p4_uart_stop()` holds UART0 in reset and restores
both pads. Received bytes reach a 1024-byte ring through interrupt source
31 on CLIC line 4. In a build with the IP kit (`TIKU_KIT_NET_ENABLE=1`),
SLIP rides the USB console, so `slip on` and the IP link need no adapter.

I2C0 drives SDA on GPIO7 and SCL on GPIO8, header P1 pins 10 and 9, as
open-drain pads through the GPIO matrix (`tiku_i2c_arch.c`). The board
pulls both lines up with 2.2 kOhm and shares them with the CSI and DSI
connectors. `tiku_i2c_init()` claims both pads and clocks the bus at 100 or
400 kHz from the crystal. A NACK ends a command batch with an error at
once, and a held line ends it within 50 ms; after a NACK that leaves the
bus busy, a STOP releases it. `tiku_i2c_close()` restores the pads and
leaves I2C0 in reset with its clocks off. BASIC's I2C words use the bus;
`/dev/i2c` is not listed, as on the C5.

GP-SPI2 drives SCK on GPIO28 and MOSI on GPIO29 and reads MISO on GPIO30,
header P1 pins 6, 7 and 8, through the GPIO matrix (`tiku_spi_arch.c`);
chip select is the caller's GPIO. `tiku_spi_init()` claims the three pads,
refuses the bus while another pad carries its clock or data output, and
clocks SCK at the 40 MHz crystal divided by the prescaler, clamped to
2..1024 and rounded up to a divider the controller has. Transfers run in
polled 64-byte batches; a batch that does not end within 50 ms returns
`TIKU_SPI_ERR_TIMEOUT`. `tiku_spi_close()` restores the pads, the MISO
input and the controller's clocks and reset. `/dev/spi/config` shows the
open configuration.

The 1-Wire master drives GPIO48, header P1 pin 11, as an open-drain GPIO
with the pad's pull-up on (`tiku_onewire_arch.c`); a bus with devices
needs an external 4.7 kOhm pull-up. The 16 MHz SYSTIMER times each slot,
so the timing does not depend on the CPU clock: a 480 us reset pulse with
the presence sample 70 us after release, 70 us write slots held low 6 us
for a 1 and 60 us for a 0, and read slots sampled 15 us after their
falling edge. Interrupts are masked through each slot and through the
presence sample. `tiku_onewire_close()` restores the pad and releases its
claim.

The SAR ADC converts one channel at a time through the LP ADC's RTC
controller (`tiku_adc_arch.c`): channels 0-7 are ADC1 on GPIO16-23, 8-13
ADC2 on GPIO49-54, and channel 30 is the temperature sensor. Header pins
A0-A3 are channels 4-7 and A4 is channel 10; the pads wired to the C6
(channels 0-3 and 13) are refused. The reference selects the attenuation:
AVCC and 2V5 12 dB, 2V0 6 dB, 1V2 2.5 dB. Each unit's initial code comes
from eFuse block 2, or is measured against internal ground on a chip
without calibration. A conversion runs with interrupts masked and restores
every register it changed; ADC2 also clocks the digital ADC for the
conversion and leaves it reset and unclocked. While the entropy source
holds the digital ADC, a read returns `TIKU_ADC_ERR_TIMEOUT`. A temperature
reading resets and powers the sensor, waits 300 us and returns its 8-bit
code; `tiku_p4_adc_celsius()` applies the eFuse correction. `adc`, BASIC's
ADC() and `/dev/adc/temp` read through the driver; there is no supply
channel.

The SHA and AES engines sit behind the crypto kit (`tiku_crypto_arch.c`,
through `hal/tiku_crypto_hal.h`), as CRACEN does on Nordic.
`tiku_kits_crypto_sha256_hash()` runs on the SHA engine, eight 64-byte
blocks per interrupt-masked chunk with the hash state reloaded between
chunks; the kit's AES-GCM runs its counter blocks on the AES engine in its
register-fed mode and GHASH in software. `/sys/crypto/mode` selects auto
(the engine, with the software path on an engine error) or sw, and
`/sys/crypto/ops` counts the operations each path served. A 4 KB SHA-256
takes about 415 us on the engine and 980 us in software. The engines' DMA
modes are not used, nor is the ECDSA engine, which ESP-IDF enables only
from silicon revision 3.0; the Olimex image builds the kit's software paths
alone.

AHB DMA pair 1 copies memory for `tiku_dma_arch_memcpy()`
(`tiku_dma_arch.c`): whole words between HP SRAM and PSRAM, up to 16
descriptors of 4032 bytes a batch, the next batch queued from the
interrupt that ends the last. Before a copy the caches are written back over
the source, and written back and invalidated over the destination, which is
invalidated again before the callback; `tiku_cpu_dcache_clean()` and
`tiku_cpu_dcache_invalidate()` act on HP SRAM through the L1 as well as on
PSRAM. SRAM to SRAM runs at about 140 MB/s. This revision's AHB DMA reaches
PSRAM without bursts: about 18 MB/s writing it and 9 MB/s reading it.

The LED PWM controller drives up to eight pins for `tiku_pwm_arch_*()`
(`tiku_pwm_arch.c`) from four timers on the 40 MHz crystal; pins at one
frequency share a timer. A timer counts 2^16 steps up to 610 Hz and fewer
above, down to 8 bits at 156.25 kHz, and a new rate or duty applies from its
next period. A free pad takes a channel through the GPIO matrix, and so
does the LED on GPIO3 while the LP core does not hold its pad.

`TIKU_USBHS_MSC=1` makes the DWC2 core on the high-speed Type-C a USB
mass-storage device (`tiku_usbhs_arch.c`, `tiku_usbhs_msc.c`). `usbmsc up`
brings the UTMI PHY and the core up, takes a 64 KB RAM disk from the SRAM
tier, lays an empty FAT12 volume, `TIKUOS`, on it and presents it: the host
enumerates it at 480 Mbps and mounts the volume. The core moves packets by
DMA past the L1 cache, so each buffer is cleaned or invalidated around it.
`usbmsc down` detaches and gives the memory back; `usbmsc stats`, `peek
<lba>` and `regs` print the counters, a block and the PHY and core
registers. The shell stays on the USB Serial/JTAG Type-C.

The HP cores' single-precision FPU runs every `float` operation: 100k fused
multiply-adds take 1.9 ms. `tiku_crt_early.S` turns it on at boot and saves
f0-f31 and `fcsr` in the interrupt frame of a context that wrote them, so
workers and interrupts each keep their own values and rounding mode; the LP
core has no FPU and its payload stays integer.

The PIE vector unit (Espressif's SIMD: eight 128-bit registers q0-q7, a
40-bit accumulator `xacc` and 216 bytes of state behind CSR 0x7F2) is on.
The upstream assembler does not know its instructions, so
`arch/esp32p4/pie/tiku_pie.inc` carries each one as a macro
(`esp_vld_128_ip q0, a0, 16` for `esp.vld.128.ip`) that checks its
operands and expands to the instruction word. `tools/esp32p4_pie_encode.py`
generated that file and `tools/esp32p4_pie_table.json` once from
Espressif's assembler (`riscv32-esp-elf-as-xespv2p1`, the instruction-set
version the v1.3 silicon executes; the `xespv2p2` words differ), solving
every operand's bit field from assembled permutations; the host test
re-encodes the table's oracle samples, so the tree needs that assembler
only to regenerate the two files:

```bash
python3 tools/esp32p4_pie_encode.py \
    --as <riscv32-esp-elf>/bin/riscv32-esp-elf-as-xespv2p1 \
    --objdump <riscv32-esp-elf>/bin/riscv32-esp-elf-objdump-xespv2p1 \
    --inventory <esp-idf>/components/esp_gdbstub/test_gdbstub_host/rv_decode/xesppie.S
```

`tiku_pie_arch.S` runs int16 and int8 dot products, an int32 add and an
int8 matrix-vector product on the unit: 4096 int16 products take 9 µs
against 148 µs in C. The trap entry saves q0-q7, the accumulators and the
shift state behind a flag when the interrupted context had written them
(CSR 0x7F2 reads dirty), as it does for the FPU, so workers and interrupts
each keep their own vector state. On this silicon `esp.vadd.u32` and
`esp.vadd.s32` saturate; the hardware-loop forms (`esp.lp.*`) are not in
the macro file.

`tiku_memcpy_arch.S` replaces newlib-nano's `memcpy`, `memset` and
`memmove`, which move a byte at a time, with the vector unit: 16 bytes a
load or store once the destination is 16-byte aligned, the shifter
(`esp.ld.128.usar.ip`, `esp.src.q.qup`) for a source that is not, words
and bytes around them, and a word path below 32 bytes. A copy with an
end outside the HP memory window (0x40000000-0x4FFFFFFF: flash, PSRAM,
ROM, L2 SRAM), such as the LP payload into LP SRAM, takes the word path
at any length, since the vector unit's 16-byte accesses fault on the
peripheral bus. A 4 KB SRAM copy
takes 2.4 µs (3.7 µs from an unaligned source) against 68 µs before, a
4 KB set 1.2 µs against 45 µs. A copy reads CSR 0x7F2 on entry and writes
it back unless the context was already dirty, so a context that only
copies keeps its initial state and its interrupts save nothing; a copy
from an interrupt leaves the interrupted context's registers to the trap
frame. Copies within PSRAM run at the PSRAM's rate: a VGA frame takes
17 ms (36 MB/s; 25 ms before), and the DW-GDMA moves the same frame in
9.7 ms.

The media blocks move pixels through the 2D-DMA (`tiku_dma2d_arch.c`),
with the caches kept coherent as for the AHB DMA. `tiku_p4_dma2d_copy()`
copies a block of 1- to 4-byte pixels between pictures in HP SRAM or PSRAM:
a 640x480 RGB565 frame from PSRAM to PSRAM takes 9.1 ms.
`tiku_p4_dma2d_convert()` copies with the out channel's colour converter on
the way: RGB888 to and from RGB565 (widening repeats a channel's low bits),
RGB888 to and from YUV444 and YUV422 (BT.601 limited range, the engine's own
matrices; YUV444 is V, U, Y in memory and YUV422 is U, Y, V, Y with U from a
pair's first pixel and V from its second), and an R/B swap. The PPA
(`tiku_ppa_arch.c`) fills, alpha-blends, and scales, turns and mirrors
ARGB8888, RGB888 and RGB565 pictures; a turn is a counter-clockwise quarter,
a mirror applies to the turned block, and scaling is bilinear in sixteenths.
A blend input may be a 1-byte picture of indices into a 256-entry ARGB8888
table loaded with `tiku_p4_ppa_lut()`, and `tiku_p4_ppa_blend_keyed()`
decides each pixel by colour keys first. A VGA fill takes 2.5 ms, doubling
320x240 to 640x480 8.8 ms. The JPEG codec (`tiku_jpeg_arch.c`) encodes
RGB888, RGB565 and grey at 4:4:4, 4:2:2 or 4:2:0, and YUV422 (U, Y, V, Y)
at 4:2:2, into a JFIF with the standard tables, its headers written in
software, and decodes baseline files of one or three components, restart
intervals and motion JPEG without Huffman tables included, to RGB888,
RGB565 or grey, or to the file's own full-range samples (V, U, Y, or U, Y,
V, Y); in channel 0 turns the decoder's YCbCr into RGB with JFIF's
full-range matrix. A VGA RGB565 frame encodes in 8 ms and
decodes in 4.5 ms. Each of these operations also has an `_async` form that
returns once the engine runs and calls back from the completion interrupt
(the 2D-DMA in channels' lines; a PPA parameter error or a JPEG codec error
ends the job through the block's own line), the engine staying claimed until
then. `tiku_dwgdma_arch.c` drives the DesignWare AXI DMA behind the MIPI
bridges and the ISP for contiguous memory-to-memory copies, the beat width
from the buffers' alignment, waited for or interrupt-completed: a VGA frame
PSRAM to PSRAM in 9.7 ms. Its channels 2 and 3 move blocks between memory
and the CSI bridge's FIFO under the hardware handshake of the ISP and the
bridge (`tiku_p4_dwgdma_chan_start()`).

`tiku_isp_arch.c` runs the image signal processor from memory, with no
camera: `tiku_p4_isp_process()` takes a job with a RAW8, RAW10 or RAW12
Bayer frame (any of the four orders) in SRAM or PSRAM, pushes it into the
ISP through DW-GDMA channel 2 (the ISP's DMA input, MIPI data types 0x2A
to 0x2C), and takes the demosaiced frame back out of the CSI bridge
through channel 3 as RGB565, RGB888 (B, G, R bytes), YUV422 (U Y V Y) or
YUV420. The job turns on the blocks it wants with their parameters: the
bilateral denoiser, the sharpener, a 3 x 3 colour-correction matrix in
2.10 fixed point, 16-point gamma curves per channel, and contrast,
saturation, hue and brightness; and asks for the frame's statistics: the
5 x 5 AE block means and the AWB white-patch count and channel sums over
a window with luminance and R/G, B/G limits. The ISP's histogram is not
offered: from memory input on v1.3 silicon its bins read zero in every
mode, window and clock setting while its frame-done flag rises.
`tiku_p4_isp_process_async()` returns once both channels run and calls
back from the DMA interrupt when both have ended. The ISP clocks at 80 MHz
from PLL240 and takes its input in bursts of four 64-bit words (bursts of
eight from HP SRAM fill its input FIFO and stall the frame); the output
comes back in 512-word bursts, the last one short. Both modules are reset
before every frame, since a frame started on what the previous one left
stalls. A VGA RAW8 frame to RGB565 takes 10.7 ms. Frames are 8 x 8 to
1920 x 1280 pixels, widths a multiple of 8 and heights even (fewer than 8
rows never leave the pipeline), buffers 8-byte aligned.
`jpeg pattern <w> <h> [rgb888|rgb565|grey] [444|422|420] [quality] [turn
<n>] [scale <n>]` draws a test card, turned or scaled on the PPA when asked,
and encodes it; `jpeg dump` prints the file in base64 and `jpeg check`
decodes it back.
`diag fault` prints the retained fault record and `diag clear` discards it;
`diag fault illegal|load|store` takes that exception (the board halts with
the record), `diag fault durable` stores into the durable span outside a
window and prints the latch; `diag pmp` and `diag pma` list the 16 entries
of each; `diag peek <hex> [words]` reads memory.

### Memory and configuration

| Resource | Firmware use |
| --- | --- |
| HP SRAM, 768 KiB physical | 572,368 bytes after excluding ROM workspace and the 128 KiB L2 cache |
| `0x4ff00000`–`0x4ff2bbd0` | Internal code/statics, durable SRAM mirror, separate retained data; remaining tail is allocator span 1 |
| `0x4ff40000`–`0x4ff9bfc0` | Allocator span 0: 376,768 bytes |
| `0x4ff9bfc0`–`0x4ffa0000` | 64-byte guard and 16 KiB foreground stack |
| PSRAM at `0x48000000` | 32 MiB mapped, 80 MHz, volatile; external requests can allocate it; supplied by LDO VO2 at 1.8 V, which the PSRAM start enables |
| Flash `0x000000`–`0x400000` | Reserved boot/code area, including the paired XIP window at offset `0x100000` |
| Flash `0x400000`–`0xff0000` | 12,517,376-byte configurable NVM region; default tier 32 KiB, remainder `/data` |
| Flash `0xff0000`–`0xff8000` | Two 16 KiB durable slots, each with a 16-byte header |
| Flash `0xff8000`–`0xff9000` | Diagnostic scratch sector; unavailable to normal firmware writes |
| Flash `0xff9000`–`0x1000000` | Reserved 28 KiB tail |

The default file extent derives 3,024 slots, including one spare, with
4,092 payload bytes per single-slot file. Directory metadata consumes space
inside the file extent; `df` reports it separately from user content. The
build can address up to 4,096 slots. Internal statics vary with build options.
Use `/sys/mem/sram_map`, `nvm_map` and `tiers` for the running build.

`/sys/psram/{state,size,hz,stage}` reports the native driver's status. Default
working-memory requests stay internal; explicitly allow or require external
memory to use PSRAM. Flash programming closes external access with dirty
PSRAM writeback and flash-cache invalidation. DMA consumers are not qualified.

`/sys/cpu/freq_available` advertises 40, 90, 180 and 360 MHz in Hz. Writing
`/sys/cpu/freq_target` saves a next-boot preference; it does not change the
live rate. `freq <MHz>` is the explicit live-rate command. Invalid rates are
refused without halting. The crystal-derived timer and USB remain independent
of the requested CPU divider. Layout ownership uses the internal SAR entropy
source and leaves the store held if identity provisioning cannot succeed.

The common `/sys/mem/layout` controls manage the NVM-region split and SRAM
reservation constraints. They do not move the linked code, stack, ROM holes,
cache partition or PSRAM mapping. No layout test automatically erases files.
The region's sector updates are not whole-filesystem power-loss transactions;
the two-slot durable mirror has its own CRC and publication protocol.

### Desktop and Bench

Connect the running device through TikuDesktop's Devices menu using its
explicit USB port. Tracker and VFS Inspector use the normal paged manifest;
Device Preferences discovers the clock choices. BASIC is available through
the existing desktop device session. The dedicated `esp32p4-usb` Bench
profile offers installed-firmware checks, not C5/C61 radio or general-kernel
categories. Both frontends pass the selected port and disallow ordinary
firmware flashing for this board.

```sh
cd TikuBench
python -m tikubench run p4-firmware --board esp32p4 --console usb \
    --port /dev/serial/by-id/USB_DEVICE --skip-build
```

The suite checks memory accounting, PSRAM attachment, invalid clock requests,
file read/write and BASIC run/SAVE. It claims absent temporary names and
refuses to overwrite an existing `prog.bas`; cleanup failure fails the run.
Select `--only memory,clock` when a user BASIC program must remain loaded.
No test in this suite reboots or changes the saved clock. Host coverage also
checks the P4 geometry, high-numbered file slots and allocator bitmap limits.

### LP core and sleep

`TIKU_LPCORE_ENABLE=1` links a payload for the LP core (RV32IMAC, on the
RC_FAST oscillator: 18.7 MHz untrimmed on the FireBeetle).
`arch/esp32p4/lpcore/` builds it with its own linker script for LP SRAM,
and the firmware carries the binary in the XIP window. The HP core copies it to `0x50108000` the first time it
starts the core (`write /sys/coproc/run 1`, `lp add`, or a boot with
durable LP jobs) and releases the core at `0x50108080`, past its vector
table, without the LP ROM.

| LP SRAM | Use |
| --- | --- |
| `0x50108000`–`0x50108080` | Vector table, 32 entries |
| `0x50108080`–`0x5010e000` | Payload image and its BSS |
| `0x5010e000`–`0x5010e800` | LP core stack, 2 KiB |
| `0x5010e800`–`0x5010f800` | Shared page: commands, mailbox, counters, LP agent |
| `0x5010fa40`–`0x5010fa80` | HP fault record |
| `0x5010fa80`–`0x50110000` | LP ROM stack and data, untouched |

The HP core reaches LP SRAM uncached. LP SRAM and the running LP core
outlive an HP reset and a deep sleep, so a boot that finds the linked
payload running adopts it instead of loading it again.

`/sys/coproc` drives the core through the portable contract
(`interfaces/coproc`). `lpcore` shows its registers and fault record and
runs the payload's commands: `led on|off|blink <passes>` drives the LED on
LP IO 3, `i2c [sda scl]` scans a bus bit-banged on two LP IOs (7 and 8 by
default), `clock` rates the LP core's cycle counter and the LP timer against
the HP clock, and `wake ms <n>` or `wake io <n> 0|1` arms a one-shot wake of
the HP core.

`power off [ms]` is deep sleep: the HP domain powers down, while the LP
domain keeps the LP timer, LP SRAM and the LP core running, and the wake is
a reset that `power why` reports (`timer`, `lp-core`, `lp-trap`). The flash
keeps its supply on this silicon revision. `power nap <ms>` is light sleep:
the core stalls on the crystal with every domain, PLL and clock gate on, so
the USB console, PSRAM, SYSTIMER and the tick run through it, at about 1 ms
a sleep. A timed sleep of either kind whose wake never comes ends in an LP
watchdog chip reset a second late.

The scheduler's DEEP and DEEPEST idle modes (`sleep lpm3`, `sleep lpm4`)
light-sleep until the next tick or hardware-timer deadline
(`tiku_p4_light_idle()`), up to about 6.8 ms at the 128 Hz tick; a deadline
under 3 ms away, or bytes queued on the USB console either way, idles in WFI
instead. LIGHT (`sleep lpm0`) is WFI, as are all three in the RAM kernel-test
image, which links no sleep code.

Boot arms the brownout detector (`tiku_p4_brownout_init()`) as ESP-IDF does
by default: below about 2.6 V its mode 0 powers the flash down and resets the
whole system, which clears the setting until the next boot; mode 1, the
fixed analog reset, is turned off.

The LP agent runs up to 8 jobs on the LP core, the HP core awake or asleep.
`lp add io <n> <op> <value> <ms> <act> [cmd...]` reads LP IO n every ms
milliseconds, and `lp add timer <ms> <act> [cmd...]` counts runs; a firing
samples the value (`sample`, listed by `lp samples`) or, during a deep
sleep, wakes the HP core (`wake`), and the shell runs the job's command
after that wake. `on /dev/gpio/1/<n> <op> <value> wake <cmd...>` writes the
same job. `/sys/lp/jobs`, `/sys/lp/samples` and `/sys/lp/wake` list the
table, the latest 16 samples and the job that ended this boot's sleep. The
table has a durable copy in the flash mirror, which a boot that finds LP
SRAM without the payload loads again.

`lp add adc <ch> <op> <value> <ms> <act> [cmd...]` converts ADC1 channel ch
(GPIO16 + ch, at 12 dB) on the LP core, and `on /dev/adc/<ch> <op> <value>
wake <cmd...>` writes the same job with a wake. While an ADC job exists the
HP's own ADC1 reads are refused, the entropy source and the jobs take turns
on the SAR through flags in the shared page, and a deep sleep keeps the HP
analog bias on (with it asleep every pad reads full scale). The HP pads lose
their IO MUX setting with the TOP domain, so asleep a job reads what drives
the pad from outside; an unconnected one reads near 0. A boot that adopts the
jobs takes ADC1 again, and the state from before the first job, kept in LP
SRAM, comes back when the last one goes.
`lp add i2c <addr> <reg> <1|2> <op> <value> <ms> <act> [cmd...]` reads a
register byte, or a big-endian word, from a device on the board's I2C pins
(GPIO7 and 8), bit-banged on the LP core; a device that does not answer reads
-1. The pins are the agent's while an I2C job exists: `gpio` and `lpcore i2c`
refuse them.

TikuBench's `coproc` and `lpcore` suites and the `deep-sleep-wake`,
`light-sleep` and `hibernate` kernel categories run on `firebeetle2_p4`.

### Qualification limits

Normal firmware, files, BASIC, PSRAM attachment, clock persistence through
ROM reset/reload and the desktop backend have hardware checks. The separate
kernel diagnostic covers scheduler/workers and allocator lifecycle. These
results do not establish the full-firmware physical cold-start behavior, nor
a brownout reset: the detector reads back armed, but no supply has been
lowered to trip it. Physical BOOT release/power cycling requires local access.

On the Olimex all 55 GPIO inputs can be observed through read-only nodes;
direction/output changes and GPIO IRQs are refused because its board header
reserves every pad. The FireBeetle 2 drives its free pads (above).
Ethernet, microSD, USB host, HDMI, audio and camera drivers are not provided
by this profile. Their wiring and attached accessories require confirmation
before external pins or power rails are driven. Wi-Fi/BLE are not integrated
P4 capabilities. CPU1 jobs are not enabled. Do not
interpret this as complete board-peripheral support.

## Storage-free diagnostic

`MCU=esp32p4 MINIMAL=1 BOARD=olimex_esp32p4_pc` builds a storage-free
native diagnostic for the Olimex ESP32-P4-PC. It supports silicon revision
1.3 and ROM ECO2. These are separate version numbers. Other revisions are
refused by the loading tool.

The diagnostic provides native CPU0
startup, a USB Serial/JTAG console, CLIC interrupts, a 128 Hz tick, one-shot
alarms, native regulator/CPLL setup, watchdog checks, SRAM checks,
exception reporting and opt-in read-only flash diagnostics. It does not run FreeRTOS or link SDK
runtime objects. Kernel, shell, BASIC, XIP, persistent storage, PSRAM,
CPU1 workers and board peripherals are not available in this profile.
Unsupported feature selections fail rather than selecting a C5/C61 port.

## Build

Use a RISC-V bare-metal GCC toolchain that supports `rv32imafc_zicsr_zifencei`
with the `ilp32f` ABI (the HP cores have a single-precision FPU and every
profile builds hard-float; the LP core payload builds `rv32imac`/`ilp32`),
and esptool 5.1.0. Python and pyserial are also needed
for loading. No ESP-IDF installation is required.

```sh
make MCU=esp32p4 MINIMAL=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    TOOLCHAIN_PREFIX=riscv-none-elf- \
    ESPTOOL=/path/to/venv/bin/esptool
```

Outputs are isolated under `build/esp32p4/diagnostic/`. `BUILD_DIR` can select
a different directory. A P4 build does not delete or replace another
platform's `main.elf` or build outputs.

The build checks the image and its matching ELF. Checks cover chip and
revision, load ranges, overlap, stack exclusion, entry point, checksum,
SHA-256, ELF ABI, instruction-set attributes, and matching section contents.
Floating-point ABI objects and extra instruction-set extensions are refused.
This is corruption and configuration checking, not authenticated boot.

## Run without changing flash

Choose the exact P4 port and its USB serial number. The tool checks the
serial number before resetting the board; choosing another Espressif port
with a different serial fails before that port is opened. It also refuses
a busy port, a non-native USB interface, a loader stub, incompatible silicon
or ROM, and an unexpected security configuration.

```sh
make ram-load MCU=esp32p4 MINIMAL=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL P4_MONITOR_ARGS='--log /path/to/new-capture.log'
```

An existing log is not overwritten. The tool loads the diagnostic through
ROM, requests identity, ping, interrupt-mask, register-preservation, clock,
alarm and watchdog checks, then repeats them across two console reopen cycles. Missing markers,
timer errors, unclaimed interrupts, counter restarts, faults, and stalled
ticks fail the run. Console open keeps DTR and RTS asserted.

The loader and the diagnostic issue no flash erase or program commands.
The diagnostic does not mount or provision a file store. Resetting the
board discards the RAM diagnostic. A normal power cycle without BOOT held
starts the installed firmware; a reset can retain forced download mode.
`make flash` and `make erase` are refused for this profile.

For a cold-start check, disconnect all P4 power sources, reconnect USB while
holding BOOT, then release BOOT before running the loader. Booting the
original firmware first is not a native cold-start test: its PLL, regulator,
cache and PSRAM initialization can affect a subsequent software-reset run.
Neither console reopen nor an RTS reset proves a power-cycle result. Pass
`--no-reset` through `P4_MONITOR_ARGS` for the first download after reconnecting
in ROM mode. This option makes one connection attempt and does not fall back
to a reset. The capture records the pre-download clock registers and image hash;
it cannot establish that physical power was removed.

Add `--fault-check --watchdog-check` for destructive **RAM execution** tests:
the first injects five exceptions, verifies their retained records across ROM
resets and RAM reloads, then clears the test record. It logs any previous
record before clearing it at the start. The second enables
a watchdog timeout after setting the reversible forced-ROM-download bit.
It reconnects without reset, requires hardware reset cause 7, then reloads
the RAM diagnostic. Neither test programs flash or eFuses. Keep other host
applications away from this board's port throughout the run.

### Read-only flash qualification

The kernel-test profile also supports SPI0 memory-mapped reads. Add
`--mmap-check` alongside `--flash-check /path/to/16MiB-backup.bin` in
`P4_MONITOR_ARGS`. This maps the fitted flash at `0x40000000`, checks the
same complete scan and clock-dependent reads, then removes the mappings.
The command is `M` for a manual device-side run. The MMU excludes pages
outside the fitted 16 MiB. Mapping changes require SRAM execution, parked
secondary cores, and no concurrent flash writes. This read test does not
establish execution from flash or cache-off writes.

Both RAM profiles accept `F` to identify and scan the board's 16 MiB EF:4018
flash. Use the guarded loader for an independently checked result:

```sh
make ram-load MCU=esp32p4 P4_PROFILE=kernel-test \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL \
    P4_MONITOR_ARGS='--flash-check /path/to/flash-backup.bin --log /path/to/new-flash.log'
```

The backup must contain exactly 16 MiB, starting at flash offset zero. It is
read and hashed before USB is opened. The loader first compares a full-chip
ROM MD5 against the backup, with the ROM geometry explicitly set to 16 MiB.
The native test then emits CRC-32 for each ordered 256 KiB block and the
complete chip. The loader compares these against the backup, including a
1 KiB read crossing offset `0x40000` at 40/90/180/360 MHz CPU clocks.
The test checks unaligned reads, end-of-chip bounds, destination guards,
unchanged status registers and advancing timer interrupts. It restores the
CPU to 360 MHz. The loader repeats the ordinary profile checks, resets into
ROM, checks the full-chip digest again, then reloads and retests the RAM image.
Hashes detect read errors and changes; they do not authenticate firmware.

The native driver attaches pads through the pinned ECO2 ROM function. It
selects XTAL as the flash clock source and configures a 10 MHz SPI1 clock.
Data transfers use single-line `0x03` reads with 24-bit addresses and at most
64 bytes per transaction; identity/status use `0x9f`, `0x05` and `0x35`.
There is no write-enable, unlock, erase, program, mount or format operation.
The driver does not map flash or depend on the ROM's default flash capacity.

Caller requirements are in `tiku_flash_arch.h`: CPU0/internal SRAM, parked
secondary cores, exclusive MSPI ownership, and no XIP or mapped external-memory
users. A busy owner or ISR caller is refused. Each transaction masks IRQs;
the original IRQ state is restored between chunks. A timeout disables further
access until reset, and a failed multi-chunk read may leave a completed prefix
in the destination. Successful transactions restore the SPI1 configuration
and FIFO contents. Clock setup remains selected for subsequent reads.

This qualifies reads against the chip's existing contents, not writable
storage, flash boot or XIP. Erased regions contain identical bytes and cannot
provide arbitrary-pattern address-alias tests without writing flash. A
physical cold run of this combined artifact is a separate requirement.

## Startup and memory

The diagnostic uses the first 64 KiB of HP SRAM, starting at `0x4ff00000`.
The upper 4 KiB is its foreground stack; separate 2 KiB interrupt and fault
stacks are included in BSS. A 64-byte fault record occupies LP SRAM at
`0x5010fa40`–`0x5010fa80`, below the ROM's LP stack/data reservation.
Link assertions refuse overlap, durable objects, and other retained objects.
ROM workspace, the upper SRAM bank, L2 cache, TCM and LP SRAM are not used
for application allocations. External flash and PSRAM are not mapped by
this diagnostic.

CPU0 masks interrupts, establishes its stack, global pointer and fixed trap
scratch area, installs a non-vectored CLIC trap handler, and clears BSS.
Startup disables inherited
boot watchdogs and holds CPU1 and the LP core in reset with their clocks
disabled. The FPU and the PIE vector unit are on with their state marked
initial (`mstatus.FS`, CSR 0x7F2) and `fcsr` at 0. The 512-byte interrupt
frame holds the integer registers and the machine return state, f0-f31 with
`fcsr` behind one flag when the interrupted context had written them
(`mstatus.FS` dirty), and q0-q7, the four `qacc` quarters, `ua_state`,
`xacc`, `sar`, `sar_bytes` and `fft_bit_width` behind another when CSR
0x7F2 reads dirty; the exit restores them before the integer registers,
writes CSR 0x7F2 back as it was, and resets `fcsr` for a context that had
no FPU state, since the interrupt may have changed it. Worker frames start
with both units in the same initial state.

The CPU first selects the board's 40 MHz crystal and verifies the clock
dividers. Native regulator initialization reads the calibrated HP/LP bias
values from eFuse, applies the documented defaults where needed, and preserves
a higher DCDC calibration value. It waits for regulator settling before
disabling the HP LDO. The CPLL is then configured for 360 MHz with bounded
analog-bus and calibration waits. Initialization failures are reported, not
treated as successful operation at a lower clock.

The `f` check exercises 40, 90, 180, 360, 180, 90, 40 and 360 MHz. CPU cycles
are measured against the independent 16 MHz SYSTIMER at each rate, along with
live IRQ register preservation and one-shot alarms. Divider programming happens
on XTAL; CPLL is selected after completion and register readback. MEM/SYS
remain at or below 180 MHz and APB at or below 90 MHz. These live changes are
for the SRAM diagnostic, **not an advertised kernel/VFS capability** with
active peripheral transfers. MPLL, cache and PSRAM initialization remain separate.

Comparator 1 supplies the `a` one-shot check. The driver cancels before
callback delivery, supports callback rearming, clamps expired deadlines,
and leaves replacement IRQ owners untouched. MWDT0 uses the 40 MHz crystal,
independent of the CPU root, with a 2 kHz prescaled counter. The `w` check
feeds it beyond its configured timeout, pauses beyond that timeout, resumes,
then disables it. Actual expiry is tested separately by `--watchdog-check`.

SYSTIMER uses comparator 0 and CPU0 interrupt-matrix source 53. The pre-v3
CLIC's priority-width field is bits 4:1, and its threshold is memory-mapped;
neither is copied from the C5 register layout. Dispatch is non-nested.
Delayed ticks are recovered arithmetically, including 52-bit counter wrap.
Source ownership conflicts and timer snapshot timeouts return errors.

The console accepts `i` for identity, `p` for ping, `c` for interrupt-mask
checks and `r` for register-preservation checks.

### Fault diagnostics

Both RAM profiles accept these explicit diagnostic commands:

| Command | Action |
|---|---|
| `d` | Read the last validated fault record, or report `P4FAULT NONE` |
| `D` | Clear that record and report `P4FAULT NONE` |
| `t` | Execute a breakpoint |
| `x` | Execute an illegal instruction |
| `v` | Attempt a load from unmapped address zero |
| `b` | Set SP to `0x10`, then execute a breakpoint |
| `n` | Execute a breakpoint inside the one-shot alarm interrupt handler |

The five injection commands stop execution; load the RAM image again to
resume. `n` takes over the diagnostic alarm, so it must not be used during
an application session. `--fault-check` runs each injection and repeats the
normal profile checks after each reload. It compares all 64 bytes through
the ROM reader before download and checks the reported record after startup.
This includes sequence numbers and CRC, not only a matching fault banner.

Trap entry saves three scratch registers and the interrupted SP in fixed
internal SRAM before using a stack. It checks alignment and room for the
integer frame in the linked BSS/foreground-stack ranges. Exceptions use a
dedicated fault stack, including exceptions raised inside an interrupt
handler. A further exception in the fault handler parks CPU0 without trying
to report recursively. These range checks are not per-worker bounds checks;
the hardware protection is the PMP NULL guard and the durable span's PMA
entries described above.

The record contains version, sequence, cause, PC, trap value, interrupted SP,
machine status, hart, raw reset-reason register, return address, context flags,
and CRC-32. `reset` describes the reset preceding the fault, not a reset
caused by the fault. Bit 0 of `flags` denotes an invalid SP; bit 1 denotes
an exception during interrupt dispatch. The handler does not read the faulting
address or walk the interrupted stack. It commits the record before bounded
USB output and does not use libc, the scheduler or console queues.

Retention is qualified for the tested native USB-to-ROM reset and RAM reload
on v1.3/ECO2. Ordinary HP SRAM did not retain the record through that path;
the dedicated LP SRAM reservation did. Startup neither clears it nor exposes
LP SRAM to the allocator. The LP core remains parked. This is not a claim
of retention across power loss, brownout, deep sleep, or execution of another
firmware image. The placement follows the pinned SDK's
[LP ROM reservation](https://github.com/espressif/esp-idf/blob/4d59230ddff16327812782151ef0afef202dc6d7/components/esp_system/ld/esp32p4/memory.ld.in).

## CPU0 kernel tests

### Experimental paired flash qualification

`P4_PROFILE=kernel-test P4_XIP_TEST=1` builds a separate qualification image
under `build/esp32p4/kernel-test-xip/`. It keeps kernel and interrupt code in
SRAM and places a small instruction/constant probe in flash. The packer
produces `diagnostic.bin`, `xip.bin`, and `diagnostic.paired.elf`. A 64-byte
descriptor binds the SRAM program to its companion by build identity and
payload CRC. The host validator also checks a SHA-256 identity derived from
both images. This detects accidental corruption and mismatched builds; it
does not provide secure boot or authenticate firmware.

The normal RAM loader refuses this profile. Flash qualification uses an
explicit tool, not `make flash`:

```sh
python tools/esp32p4_flash.py \
    --image build/esp32p4/kernel-test-xip/diagnostic.bin \
    --elf build/esp32p4/kernel-test-xip/diagnostic.paired.elf \
    --xip build/esp32p4/kernel-test-xip/xip.bin --check-only
```

To run on hardware, replace `--check-only` with `--first-install
--restore-after --backup /path/to/full-backup.bin --log /path/to/new.log`
and specify the P4's `--port` and `--serial`. The tool requires esptool 5.1.0
and checks security, revision and flash identity before any write. It prints
the exact boot and companion sector extents. The complete existing flash
must match the backup. It writes the companion before the boot image, verifies
both, and checks that every other byte remains unchanged.

The qualification requires ROM flash boot, two kernel runs, and command `X`
for instruction/constant fetch at four CPU rates with live interrupts.
`--restore-after` restores the affected original sectors and verifies the
whole chip before returning to the RAM kernel. A failed qualification also
attempts restoration. `--restore-original` with the same image pair and backup
retries recovery; it refuses unrelated flash changes. Keep the backup even
after a successful test. The non-XIP kernel image uses the same tool without
`--xip` and with its ordinary ELF.

Native flash startup and the four-rate XIP probe have passed on the connected
v1.3/ECO2 board. This qualifies the probe, not placement of the complete
kernel in flash or writes while executing XIP. The installer refuses to write
when BOOT was sampled low: an ordinary USB core reset does not resample that
state. Release BOOT and power-cycle before retrying. Keep physical cold-start
results separate from software-reset qualification.
Persistent mirrors, TFS, PSRAM and peripheral support are not enabled by
this build. `make flash` and whole-chip erase remain refused.

### Native 32 MiB PSRAM qualification

`P4_PROFILE=kernel-test P4_PSRAM_TEST=1` builds an independent SRAM-only
image under `build/esp32p4/kernel-test-psram/`. The driver initializes MPLL,
the dedicated pads and SPI2/SPI3 controllers for an 80 MHz x16 interface.
It requires the fitted AP 32 MiB part, checks mode-register readback and direct
transfers, and maps exactly 512 64-KiB pages. Initialization checks the mapped
low byte lanes against the direct-controller pattern before publishing capacity.
SPI3's 8-bit transfers and SPI2's x16 mapping have different byte packing.
The full-memory test below exercises both mapped byte lanes.

No SDK runtime or PSRAM-backed stack is used. The caller must execute from
internal SRAM with the other cores parked, no mapped external-memory users,
and no DMA. The initial connection check modifies 64 low-lane bytes within
the first 128 bytes. A failed initialization leaves capacity unpublished and
requires a reset before retrying. MMU setup failures clear the page table and
reenable the cache; a failed MPLL setup does not access the unclocked SPI bus.

```sh
make ram-load MCU=esp32p4 P4_PROFILE=kernel-test P4_PSRAM_TEST=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL \
    P4_MONITOR_ARGS='--psram-check --log /path/to/new-psram.log'
```

The explicit `P` command destroys unallocated PSRAM contents. It refuses a
currently attached external tier. Use the guarded loader, which requires a
matching PSRAM-test image before opening the port. The test covers:

- Four complete 32 MiB write/read patterns, at 40/90/180/360 MHz CPU rates,
  with advancing timer interrupts. Writing the complete range before reading
  exceeds the cache capacity and checks for address aliasing.
- Byte and halfword updates across cache lines, 64 KiB pages, the 16 MiB
  midpoint and the last bytes, followed by cache-evicting reads.
- Explicit and external-allowed allocation, exclusion from default placement,
  arena/pool release, live-user detach refusal and complete capacity recovery.
- Three whole-device arena reservations and attach/detach cycles; forced
  detachment invalidates old arena/pool handles, including after reattachment.

The test leaves the external tier detached, restores the CPU to 360 MHz, and
runs the ordinary kernel suite again. Physical PSRAM mapping remains enabled.
The host requires the complete ordered transcript and a sequential kernel run
number; a reset cannot substitute for successful cleanup.

For flash-boot qualification, pass this profile's image and ELF to
`tools/esp32p4_flash.py` with `--psram-check`, `--first-install`,
`--restore-after` and the same backup/identity arguments described above.
It runs the destructive PSRAM test after native boot, restores the original
flash sectors, verifies the whole-chip backup and reloads the RAM kernel.
Do not combine this profile with the scratch writer or XIP probe. These
tests do not qualify flash writes with live PSRAM, DMA, PSRAM power-down,
the complete firmware profile, or physical cold-start behavior.

### RAM kernel profile

`P4_PROFILE=kernel-test` selects a separate storage-free image that links the
portable process, scheduler, timer, critical-window and worker implementations.
CPU0 runs at 360 MHz; CPU1 and the LP core remain parked. Do not pass `MINIMAL`
or enable ordinary firmware features with this profile.
The toolchain must provide newlib headers and `libc`/`libnosys` for `ilp32f`.

```sh
make ram-load MCU=esp32p4 P4_PROFILE=kernel-test \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL \
    P4_MONITOR_ARGS='--log /path/to/new-kernel-capture.log'
```

The image and ELF are under `build/esp32p4/kernel-test/`. This profile uses
the two SRAM ranges below; the diagnostic retains its 64 KiB limit. The
loader checks each profile's boundaries, plus revision, ABI and USB identity.
Nothing is installed in flash. Newlib heap growth is refused;
libc file-descriptor I/O is unavailable. Linker warnings about unsupported
newlib system calls do not indicate a filesystem implementation.

The test programs are `TikuBench/tests/esp32p4/test_kernel.c` and
`test_memory.c`. Command `k` runs 84 assertions covering process events and coalesced polls, software and hardware
timer cancellation, nested atomic sections, critical IRQ masks, worker
start/join/reuse, register preservation, preemption, kernel priority, cycle
budgets, queue waits/timeouts, stack canaries, WFI wakeup and errno isolation
between the kernel, workers and interrupts. The FPU and the PIE vector
unit are on; `fpu-p4`, `pie-p4` and the threads category check their state
across interrupts and worker switches.
Memory checks cover both SRAM spans, full-range address-dependent patterns,
alignment, arenas, pools, workspace aliases, non-LIFO release, coalescing,
stale handles, metadata exhaustion, software stack guards, secure wiping and
allocator refusal in interrupt and worker contexts. Reconstruction tests on
each span inject an owner restore failure, verify that reservations remain
unavailable, retry recovery, and require all capacity to be returned.

The loader requests a fresh run after each console reopen, rejects missing or
misordered assertions, failed summaries, traps and stalled counters, and checks
that the run number advances without a reboot. `--fault-check` also works
with this profile; `--watchdog-check` requires the diagnostic profile.
A physical cold run requires power removal and `--no-reset`, as
for the diagnostic.

This profile does not provide persistence, PSRAM, shell, BASIC, the general
TikuBench firmware runner or board peripherals. Its test adapter reports no
storage layout; it cannot provision `/data`. NVM and PSRAM reservations fail,
NVM commit reports an error, and durable-image access returns no image.

### Kernel SRAM layout

| Range | Use |
|---|---|
| `0x4ff00000` to linked BSS end | Code, constants, data and BSS, including allocator metadata, worker, ISR and fault stacks |
| Linked BSS end to `0x4ff2bbd0` | Extra SRAM allocation span (span 1) |
| `0x4ff2bbd0`–`0x4ff40000` | Excluded ROM/boot workspace |
| `0x4ff40000`–`0x4ff9efc0` | Primary SRAM allocation span (span 0) |
| `0x4ff9efc0`–`0x4ff9f000` | 64-byte foreground stack guard |
| `0x4ff9f000`–`0x4ffa0000` | 4 KiB foreground stack |
| `0x4ffa0000`–`0x4ffc0000` | Reserved 128 KiB L2 cache |
| `0x5010fa40`–`0x5010fa80` | 64-byte retained fault record, excluded from the allocator |

Ranges exclude their upper endpoint. TCM, LP SRAM, external-memory aliases
and the ROM workspace are not allocator capacity. A single allocation must
fit one span. The extra span follows actual linked sections, not a fixed
growth allowance. Linking requires at least 16 KiB there and 64 KiB in the
primary span. The build prints the current byte budget. The loader also
checks allocator symbols against BSS and the explicit stack sections.

Before using the high-SRAM stack, startup runs on the low-SRAM ISR stack,
parks the other cores, and calls the ECO2 ROM cache-mode and invalidation
functions. It selects 128 KiB L2 with 64-byte lines and checks the registers.
Reset defaults do not establish that geometry. A failed setup parks CPU0;
the host receives no passing test transcript. No SDK runtime is linked.

Both foreground and ISR stacks have 64-byte software sentinels. These detect
corruption when checked; they do not prevent an access. Hardware MPU/PMP
protection is not enabled by this RAM profile and its control query returns
zero. There is no claim of process isolation; retained records preserve
diagnostics, not application state.

## Scratch-sector write qualification

`P4_FLASH_WRITE_TEST=1` adds erase/program tests to the kernel RAM profile.
It builds in `build/esp32p4/kernel-test-write/` and uses the separate
`tools/esp32p4_flash_test.py` loader. Ordinary RAM profiles contain no write
commands, and the ordinary loader rejects the write-test image.

**This test changes flash**, but only the 4 KiB sector at physical offsets
`0xff8000` through `0xff8fff`. It requires a complete 16 MiB backup with that
sector erased and the original vendor partition table at `0x10000`. The
table checksum and partition bounds must be valid; no partition may overlap
scratch. It does not install firmware, mount a filesystem, clear protection
bits, or change eFuses.

```sh
make ram-load MCU=esp32p4 P4_PROFILE=kernel-test P4_FLASH_WRITE_TEST=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL \
    P4_MONITOR_ARGS='--backup /path/to/flash-backup.bin --log /path/to/new-write.log'
```

Before writing, the tool requires the device's whole-chip ROM digest to
match the backup. The native driver bounds every write to scratch, checks
WEL/WIP, splits transfers at page boundaries, and verifies readback. A request
with any 0-to-1 bit transition is refused before programming any of its bytes.
Sources must be internal SRAM; a sector-sized internal buffer stages each
request. The SRAM MSPI guard masks IRQs and branch prediction, writes dirty
L1 data back twice using the ECO2 cache-maintenance sequence, suspends L2,
and closes the SPI0 cache path. IRQs remain masked from write-enable through
flash-busy completion. Before returning, the guard invalidates the changed
flash lines, resumes L2 without discarding dirty PSRAM data, and restores
the caller's AXI, prediction and interrupt state. Interrupts can run between
program chunks, once flash is readable again.

Controller waits, cache synchronization polls and flash-busy polling have
fixed limits. L2 suspend/resume use the silicon's ROM functions. A mutation
that leaves flash state uncertain, or a failed cache synchronization, traps
in SRAM with IRQs masked. This avoids returning to unreadable or stale flash
code; reset/reload is required. No status-unlock or automatic flash-reset
command is issued.

The test covers invalid ranges, unaligned sources, page crossings, illegal
bit transitions, and full-sector patterns at 40/90/180/360 MHz. The write
profile also maps flash and initializes the fitted PSRAM. Before each erase
and program test, it dirties three 8 KiB PSRAM ranges: the start, across
16 MiB, and the last 8 KiB. It primes the scratch flash cache, then checks
that PSRAM retains its data and that mapped flash agrees with SPI1 readback.
The host requires one `P4MSPI` initialization record and both checks at each
CPU rate. This test refuses to overwrite PSRAM attached to the allocator.

It leaves a known pattern, resets into ROM, and checks that the whole chip
matches the backup with just that sector changed. A RAM reload verifies the retained
pattern. The tool then checks bytes outside scratch, erases scratch, and
requires the original whole-chip digest. Kernel checks run around these
operations. This establishes the tested software-reset path, not power-loss
atomicity or physical cold-start behavior.

If a test fails after writes begin, the tool attempts scratch restoration
once. If interrupted or restoration fails, keep the backup and rerun with
`--restore-scratch` added to the same arguments and a new log. Restoration
refuses if bytes outside scratch no longer match the backup. An interrupted
erase/program can leave scratch incomplete. Do not use these commands with
valuable data in that sector.

The loaded test is idle until it receives a complete framed command:
`!P4:WRITE:FF8000!`, `!P4:VERIFY:FF8000!`, or `!P4:RESTORE:FF8000!`, followed
by a newline. Use the guarded host tool; typing a command directly bypasses
its backup checks. The build/capability marker prevents profile mistakes;
it is not authentication. After qualification, load the ordinary kernel
profile without `P4_FLASH_WRITE_TEST=1` to remove these commands from RAM.

These operations are qualified for SRAM execution with CPU1/LP parked,
mapped flash data, dirty PSRAM and no DMA. The caller, its constants, stack
and write buffers remain in internal SRAM. These tests do not execute XIP
instructions around writes, qualify DMA/cache coherency, or enable arbitrary
flash-region writes, durable mirrors or TFS.

## Persistent-storage qualification

`P4_PROFILE=kernel-test P4_STORAGE_TEST=1` builds a separate SRAM-only image
under `build/esp32p4/kernel-test-storage/`. It cannot be combined with the
scratch, PSRAM or XIP profiles. This diagnostic does not provision storage
or mount the normal firmware's file store.

The native region backend provides mapped reads and sector-preserving writes
within `[0x400000,0xff0000)`. It stages each affected 4 KiB sector in SRAM,
erases only when a bit must change from 0 to 1, and preserves neighboring
bytes on a successful update. Identical writes do not program flash. Sources
overlapping an affected sector, ISR calls, reentrant writes and invalid
ranges are refused. **A power cut during a region update can damage that
sector.** This backend does not make a multi-sector update atomic.

Durable objects use an SRAM working copy and two 16 KiB flash slots at
`0xff0000` and `0xff4000`. Each slot has a 16-byte header and a maximum
16,368-byte payload; the linker enforces that limit. Initialization is
explicit, after flash mapping is ready, and does not write flash. It restores
the newest valid CRC-checked image. A smaller saved image leaves the remaining
working bytes zero; a larger one is restored up to the working size, and
`tiku_persist_move_boot()` moves persist cells from the whole saved image by
key. If neither slot is valid, both must be completely erased before commits
are enabled. Nonblank corrupt storage is held without formatting. The region
registry lists the working copy as NVM, so `tiku_persist_register()` accepts
buffers placed with `TIKU_DURABLE`.

A commit snapshots the working copy with interrupts masked, writes the
alternate slot, publishes its magic last, and checks the resulting image
before reporting success. The previous selected slot remains intact. An
unchanged snapshot skips the write. `tiku_mpu_lock_nvm_status()` propagates
commit failure. P4 has no hardware write protection for this SRAM copy;
applications must follow the unlock/write/lock contract themselves.

Use the guarded loader with an existing full-chip backup and a new log:

```sh
make ram-load MCU=esp32p4 P4_PROFILE=kernel-test P4_STORAGE_TEST=1 \
    TOOLCHAIN_DIR=/path/to/riscv-toolchain \
    ESPTOOL=/path/to/venv/bin/esptool PYTHON=/path/to/venv/bin/python \
    PORT=/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00 \
    P4_SERIAL=SERIAL \
    P4_MONITOR_ARGS='--backup /path/to/flash-before.bin --log /path/to/new-storage.log'
```

`tools/esp32p4_storage_test.py` requires the whole writable reservation to be
erased in the backup, excluded by its valid vendor partition table, and
identical to the live chip before testing. The ordinary RAM loader and flash
installer refuse this image. Its marker identifies the profile; it does not
authenticate commands.

The native test modifies only two region sectors `[0x400000,0x402000)` and
the two mirror slots `[0xff0000,0xff8000)`. It tests bounds, cross-sector
updates, preserved neighboring bytes, two 5,000-byte snapshots, unchanged
commit suppression and an alternate payload without a published header.
The host checks the exact whole-chip result through ROM, reloads to test
recovery, and restores the ten test sectors. Restoration first checks all
other bytes, then requires the original whole-chip digest. An interrupted
test can be restored with `--restore-storage` and a new log. If bytes outside
the test extents changed, restoration is refused.

Complete framed commands are `!P4:STORE:400000!`, `!P4:RETAIN:400000!` and
`!P4:CLEAR:400000!`, each followed by a newline. Direct commands bypass the
host backup checks. Load the ordinary kernel image after qualification to
remove the writable commands from RAM.

This profile qualifies the backend and mirror with SRAM execution and
software resets. It does not publish an NVM allocator tier, mount TFS,
implement layout provisioning, or qualify physical power cuts. Normal
startup, shell/VFS/BASIC storage integration, XIP callers during writes,
DMA and simultaneous storage/PSRAM qualification require separate work.

## Host tests

```sh
make -C TikuBench/tests/host/kernel esp32p4-boot
make -C TikuBench/tests/host/kernel esp32p4-boot \
    EXTRA_CFLAGS='-fsanitize=undefined -fno-sanitize-recover=all'
```

These tests run without opening any board. Register models compile the
production C sources, test error paths and deliberately mutated variants.
Fault models reject corrupt records, interrupted commits, sequence errors,
unbounded USB waits and missing final USB packets. ELF fixtures require the
exact non-loadable LP record reservation and reject incoming bytes there.
The shared USB FIFO and host console helpers also retain C5 regression
coverage. P4 is not advertised as a qualified kernel-runner board by these
diagnostic tests.

Scratch models compile the production write driver and command parser.
They cover protection, write-enable failure, partial readback, frozen time,
stuck hardware, page boundaries, source staging, IRQ state and reentry.
Host checks reject malformed backups, overlapping partitions, incomplete
test records and restoration when bytes outside scratch have changed.
Cache-exclusion models check L1 writeback order, flash-only invalidation,
line rounding, nested/ISR refusal, prior AXI/prediction/IRQ state, and
fail-closed behavior for stalled cache/SPI0 or uncertain flash state. A data
model checks the coexistence diagnostic detects PSRAM corruption and stale
flash contents at its tested boundaries.

Storage models simulate each byte-cut position in a 5,000-byte mirror update,
require the previous or complete new image after reboot, and cover sequence
wrap, corrupt/unpublished slots, changed image sizes, unchanged commits,
reentry, ISR refusal and erase/program/readback failures. Region tests cover
cross-sector preservation, source aliasing, read/write failures and range
limits. Host loader tests reject occupied storage and unsafe restoration.

## Register references

Register definitions follow Espressif ESP-IDF revision
`4d59230ddff16327812782151ef0afef202dc6d7`, specifically
`components/soc/esp32p4/register/hw_ver1/soc/`, the P4 CLIC definitions,
and the P4 clock and SYSTIMER HALs. No SDK library is linked.

- [P4 revision 1.3 datasheet](https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_datasheet_en.html)
- [ROM serial protocol](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/serial-protocol.html)
- [Olimex board design](https://github.com/OLIMEX/ESP32-P4-PC)
- [DFRobot FireBeetle 2 ESP32-P4 (DFR1237)](https://wiki.dfrobot.com/dfr1237/)
