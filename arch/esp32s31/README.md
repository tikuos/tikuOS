# ESP32-S31 Korvo-1 native port

The port runs native TikuOS on CPU0 of the ESP32-S31-Korvo-1, silicon revision
0.0. It builds for `rv32imafc_zicsr_zifencei` with the single-float ABI
`ilp32f`. CPU, memory, system and APB clocks use the 40 MHz crystal. HP1 is
stalled and LP execution is disabled. The kernel-test linker uses 64 KiB of HP SRAM,
including a 4 KiB kernel stack; it rejects persistent and retained sections.

The runtime provides non-nested CLIC interrupts, a 128 Hz kernel clock,
a 1 MHz hardware timer, processes, preemptible workers, integer/FPU context
preservation, per-context C-library state and a timer-group watchdog.
Startup disables the bootloader LP watchdog and its flash-boot mode before
the application selects its watchdog policy.
PIE and hardware-loop context are not supported. Faults print their cause
and PC through UART0 and halt with machine interrupts masked.

UART0 uses GPIO58/59 at 115200 baud, 8N1, with an interrupt-fed receive queue
holding 1,023 bytes. I2C0 uses the Korvo's pull-ups on SDA GPIO0 and SCL GPIO1
at configured rates of 100/400 kHz. Transfers preserve repeated START across
FIFO batches and use bounded waits. The receive batches hold at most 31
bytes so the final NACK can complete. Closing the bus restores its pin state.
GPIO claims cover GPIO0/1, audio GPIO3..7, the 21 RGB-panel pads and card GPIO20..25/39. GPIO58/59 are reserved
for the console; other pads report unreadable.

The flash shell boots through the installed Korvo second-stage bootloader.
It maps constants and an identity routine in two flash regions and loads
the kernel into SRAM. The shell attaches 16 MiB of PSRAM to the kernel memory tiers. The memory
qualification profiles test allocation and AHB DMA transfers; the PSRAM
profiles test the full device capacity. Display profiles exercise RGB scanout
and touch polling. The SD profile exercises SPI2 and read-only SDHC/SDXC probing.
The audio profile exercises stereo I2S and ES8389 capture into PSRAM.
The `firmware` profile adds native station/AP Wi-Fi, DHCP, shell networking
and encrypted credentials in a reserved 8 KiB flash region. RAM loading
and warm-reset tests do not qualify true cold boot. The peripheral profiles
do not write flash or an SD card; the host flash qualification tool
temporarily replaces application sectors and restores the supplied backup.

## Build profiles

Use the Espressif RISC-V toolchain and a Python environment containing
`esptool==5.6.0`:

```sh
make MCU=esp32s31 BOARD=esp32s31_korvo1 S31_PROFILE=kernel-test \
    CROSS_COMPILE=/path/to/toolchain/bin/riscv32-esp-elf- \
    PYTHON=/path/to/venv/bin/python
```

| `S31_PROFILE` | Contents |
| --- | --- |
| `kernel-test` (default) | Process, worker, FPU, C-library, timer, interrupt-mask and populated-board I2C checks from TikuBench |
| `board-test` | Kernel checks plus the optional `drivers/input/gt1151` identity and touch-poll checks |
| `watchdog-test` | Feed, pause, resume and disable checks followed by an intentional 250 ms watchdog reset |
| `shell` | SRAM and PSRAM allocation, native shell with `help`, `ps`, `timer`, `queue`, `echo`, `calc`, `htimer` and `clear` |
| `firmware` | Flash shell with native station/AP Wi-Fi, SAE/PMF, DHCP, `wifi`, `ip`, `ping` and encrypted profiles |
| `flash-shell` | The shell with application metadata and two flash mappings for the factory bootloader |
| `psram-test` | Full-capacity patterns and physical cache-visibility checks with timer interrupts active |
| `flash-psram-test` | The PSRAM checks through the factory bootloader |
| `memory-test` | Tier allocation, AHB DMA cache handoff, context refusal and transfer recovery |
| `flash-memory-test` | The memory checks through the factory bootloader |
| `display-test` | RGB565 scanout, GPIO readback, drawing, touch and fault recovery |
| `flash-display-test` | The display checks through the factory bootloader |
| `sd-test` | SPI2 loopback, timeout recovery and read-only SDHC/SDXC inspection |
| `audio-test` | I2S loopback, queue recovery, ES8389 control and a minute of PSRAM capture |

Images are `build/esp32s31/<profile>/kernel.bin`. These profiles do not use
`main.elf`. The shell profile has no durable aliases or history and arms a
two-second watchdog, fed from its foreground dispatch loop. The other
profiles leave the watchdog off except during its explicit test.

The integer-only initial diagnostic remains available through
`make -f arch/esp32s31/diagnostic.mk`. Its output is
`build/esp32s31-diagnostic/diagnostic.bin`; it checks 4 KiB of SRAM,
100 ECALL returns, 100 software interrupts and 1,000 timer interrupts.

## Hardware runner

Select the CP2102N bridge by device path and serial identity. The Linux
runner checks port ownership, image target and SRAM segment bounds before
loading. It restores the installed flash firmware by reset on exit.

```sh
python tools/esp32s31_ram.py \
    --image build/esp32s31/board-test/kernel.bin \
    --port /dev/serial/by-id/<bridge> --serial <bridge-serial> \
    --board-test --runs 5 --seconds 6 --log /path/to/new-run.log
```

Use `--kernel-test`, `--shell-test`, `--watchdog-test`, `--psram-test` or
`--memory-test`, `--display-test`, `--sd-test` or `--audio-test`
for those profiles;
omit the profile flag for the integer diagnostic. `make ... ram-load`
selects the flag and takes `PORT`, `S31_SERIAL` and `LOG`. The shell runner
sends commands and checks 16 distinct echo replies from a queued burst.
The watchdog runner requires the S31 ROM's `HP_SYS_HP_WDT0_RESET` reason
within 150–400 ms of the expiry marker. Because RAM loading selects download
boot, this reset returns to the ROM loader; the runner then resets into the
installed firmware. Companion JSON and factory-boot logs accompany each run.

## Flash-boot qualification

Build with `S31_PROFILE=flash-shell`. This image requires the installed
second-stage bootloader and cannot be loaded by the RAM runner. The host
tool requires a 16 MiB factory backup and its SHA256, checks that the
application sectors and partition table match the board, then writes the
image and exercises the shell after each reset. It restores and verifies
the original sectors on exit, including after a failed test or partial
write. A disconnected board can prevent restoration; the companion JSON
records whether restoration completed and identifies the backup.

```sh
python tools/esp32s31_flash.py \
    --image build/esp32s31/flash-shell/kernel.bin \
    --backup /path/to/factory-flash.bin --backup-sha256 <sha256> \
    --port /dev/serial/by-id/<bridge> --serial <bridge-serial> \
    --runs 20 --log /path/to/new-flash-run.log
```

The runner accepts unencrypted, unsecured S31 revision 0.0 with the Korvo
factory application at `0x10000`. It leaves the bootloader, partition
table, NVS and asset partition in place. Flash constants remain accessible
after TikuOS selects its 40 MHz clocks. Firmware flash writes, cache-off
interrupts and booting through a TikuOS-owned bootloader require separate
qualification.

## PSRAM ownership

The PSRAM backend initializes the Korvo's AP octal 16 MiB device with its
1.8 V supply and a 20 MHz bus derived from the 40 MHz crystal. It verifies
mode registers, a physical transfer and 256 mapped 64 KiB pages. PMA entry
0 grants cached read/write access to `0x50000000..0x50ffffff`; execution is
disabled for that span. Initialization requires CPU0 boot with interrupts
masked, HP1 parked and no existing PSRAM or DMA users. The first 64 bytes
hold its startup test pattern before capacity is published.

`tiku_s31_psram_sync()` writes back owned, aligned 64-byte cache lines and
optionally invalidates them. It applies the S31 two-command writeback
sequence with bounded waits. A timeout removes published capacity and
requires reset before further PSRAM access. Power-management integration is
absent. `tiku_s31_psram_invalidate()` discards lines after a DMA write without
writing stale CPU contents back to the device.

`tiku_s31_memory_init()` runs at boot with interrupts masked. It attaches
PSRAM to `TIKU_MEM_PSRAM` and initializes a 64-byte-aligned SRAM backing span
between the linked statics and the 4 KiB kernel stack. Repeated calls preserve
live reservations. Portable callers request PSRAM explicitly or permit it
with `TIKU_MEM_ALLOW_EXTERNAL`; ordinary AUTO requests stay in internal memory.
An allocation intended for DMA needs 64-byte-aligned backing and whole cache
lines. The NVM tier remains absent.

`tiku_s31_dma_copy()` reserves AHB DMA pair zero and runs bounded foreground
copies between internal SRAM and PSRAM, up to 1 MiB per call. It rejects
workers, interrupt context, overlapping spans and partial cache lines. The
caller owns both spans and their allocation backing through return. Source
writeback precedes DMA; destination writeback/invalidation precedes DMA and
invalidation follows completion. Eight SRAM descriptors per direction cover
32,256 bytes per batch. Both channels must finish and return RX descriptors
before a batch succeeds. Errors and a 100 ms batch timeout stop and reset the
pair. Failure to establish parked links halts execution.

The AHB bus master uses TEE master 48 in trusted mode; initialization refuses
a locked incompatible mode. Memory-region filters remain enabled. This
privileged driver does not provide DMA isolation for untrusted callers.
The copy path polls completion with timer interrupts enabled and does not
provide asynchronous peripheral queues.

Run `memory-test` with `--memory-test --seconds 30`, or use
`flash-memory-test` and the same options with the flash runner. Its workload
covers four source/destination memory combinations, descriptor boundaries,
1 MiB transfers, canaries, worker/interrupt refusal, descriptor errors,
timeout recovery and 32 repeated 800×480 RGB565-sized transfers. It verifies
memory contents; it does not drive a display.

The qualification test writes two complementary address-dependent patterns
across 16 MiB, invalidates after writeback and verifies the full span. It
also checks physical contents through SPI3, observing stale words before
writeback and updated words afterward. Run with `--psram-test --seconds 12`.
Use `flash-psram-test` and add `--psram-test --seconds 12` to the flash tool
to check initialization through the installed bootloader. Flash qualification
defaults to 15-second windows and rejects a second ROM boot within a window.

## RGB display

The display backend implements the portable `interfaces/display` API for
800×480 RGB565, with 1,600-byte rows and a 64-byte-aligned PSRAM framebuffer.
Initialization requires attached memory tiers and kernel foreground context.
It reserves AXI DMA TX channel zero, the LCDCAM block and 21 Korvo pads:
data on GPIO8..19 and GPIO33..36, PCLK on GPIO40, DE on GPIO43, HSYNC on
GPIO44, VSYNC on GPIO45 and display enable on GPIO38. The display build
sets `TIKU_GPIO_CLAIM_LIMIT=32` so I2C and LCD claims coexist.

The pixel clock is 5 MHz from the crystal. Horizontal sync/back/front
intervals are 40/40/48 clocks; vertical intervals are 23/32/13 lines.
The 928×548 total frame gives about 9.83 Hz scanout. Higher pixel clocks
require qualification with concurrent memory traffic. GPIO38 has no
brightness control through this backend.

The caller retains its framebuffers and display structure until
`tiku_s31_display_close()`. Initialization reserves a 768,000-byte PSRAM
scanout surface and two rings of 191 SRAM descriptors, 6,112 bytes in total.
In single-buffer mode, rectangle fills and clears write the caller's buffer.
Present stops scanout and copies the cache-line-expanded span from the first
damaged pixel to the last through AHB DMA, including intervening rows. It
restarts at the ring's beginning and waits for two frame boundaries with a
300 ms deadline. Single-buffer updates interrupt refresh.

The backend advertises `TIKU_DISPLAY_CAP_FLIP`. Use
`tiku_display_set_buffers()` with two disjoint, 64-byte-aligned, complete
PSRAM surfaces. Drawing targets the back buffer; flushing records no
scanout change. `tiku_display_flip()` publishes the back buffer's cache lines
in 256-byte batches, redirects the current descriptor ring at its frame
boundary and waits for the new ring's EOF before returning the old front
buffer for drawing. Scanout stays active during publication and handoff.
The wait has a 500 ms deadline; a failed handoff stops DMA and requires close
and reopen. Calls that draw into the current scanned surface are refused.
Direct caller writes must obey the same ownership rule. The private surface
remains reserved until close, including in paired mode. Circle and
rounded-rectangle acceleration are absent.

GPIO33/34 carry RGB data and also connect to the USB Serial/JTAG PHY.
Initialization disables that PHY's pads and overrides its pull resistors
while LCD owns the pins. Close restores the saved USB pad configuration.
The UART0 console uses GPIO58/59.

Poll `tiku_s31_display_poll()` from the foreground more often than one frame
period to collect VSYNC, DMA EOF, underrun and DMA-error observations.
Polling can coalesce events when delayed; the counts are observations, not
hardware cumulative counters. An error stays latched until close and reopen.
Close stops requests and establishes a parked DMA link before releasing
storage and restoring pad state. Failure to establish stopped DMA or to
release its owned reservation halts execution. Memory-tier resets, forced
detach and direct access to the private scanout surface are forbidden while
open. Calls from workers or interrupt handlers are refused.

AXI DMA uses trusted mode through TEE entry 64. SRAM descriptor faults from
TX0 report bus ID 72. A locked incompatible mode is refused; memory-region
filters remain enabled. The LCDCAM block and AXI TX0 belong exclusively to
this backend in the display profiles. Camera sharing and general DMA-channel
allocation are absent.

Run `display-test` with `--display-test --seconds 35`. The test requires the
optional Goodix driver checkout and exercises 100 Hz touch polling during a
ten-second scanout interval. It reads VSYNC, enable and RGB data through
GPIO inputs, verifies copied framebuffer contents with scanout stopped,
and checks 16 paired solid-color frames with touch polls between flips.
A 1 kHz timer samples VSYNC across blocking handoffs. Tests also cover
clipped drawing, full-screen clear, ownership conflicts, worker/interrupt
refusal and recovery after injected descriptor, handoff and underrun faults.
Use `flash-display-test` and the same runner options with the flash tool
for bootloader qualification.

For a timed run, build `display-test` with `S31_DISPLAY_SOAK_SECONDS=1800`
and run the RAM tool with `--display-test --display-soak-seconds 1800
--seconds 1835`. The runner streams UART output to its log and requires both
host and firmware duration evidence. The firmware repeats full-frame colors
and pad checks, polls touch between flips, and reports progress each minute.
These checks do not establish visible color order, panel image quality,
touch-coordinate alignment or touch latency. They do not qualify true cold
boot or simultaneous audio, SD and radio traffic.

## Register sources

Register definitions and clock behavior were checked against ESP-IDF
[`4d59230`](https://github.com/espressif/esp-idf/tree/4d59230ddff16327812782151ef0afef202dc6d7):
S31 `hp_sys_clkrst_reg.h`, `interrupt_core0_reg.h`, `gpio_reg.h`,
`io_mux_reg.h`, `uart_reg.h`, `i2c_reg.h`, `timer_group_reg.h`,
`cpu_utility_ll.h`, `systimer_ll.h`, `i2c_ll.h`, `mwdt_ll.h`,
`ahb_dma_reg.h`, `ahb_dma_ll.h`, `axi_dma_reg.h`, `axi_dma_ll.h`,
`lcd_cam_reg.h`, `lcd_ll.h`, `gpio_ll.h`, `usb_serial_jtag_reg.h`, `tee_reg.h` and `hp_mem_apm_reg.h`.
The board wiring is described in the
[Korvo-1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html).
The S31 radio archives use the same single-float ABI. `phy-test` links the
PHY archive; `flash-wifi-test` and `firmware` link the Wi-Fi and PHY archives.
The other profiles do not link radio archives. The normal `firmware` build
uses 384 KiB of SRAM, including an 8 KiB kernel stack, and maps vendor radio
code from flash. Its idle loop yields to radio workers before sleeping.

Build with `S31_PROFILE=firmware S31_WIFI_COUNTRY=SG`, substituting the
installed country's code. Use the guarded flash runner's `--firmware-test`
option to exercise shell scanning, WPA2/SAE AP startup and the AP IP service;
the runner restores the factory application afterward. This smoke test does
not write settings or establish client authentication. Add `--settings-test --runs 2` to save a fixture profile, verify locked reboot and wrong-key
refusal, unlock and forget it. This mode requires erased settings sectors
matching the backup and restores and verifies those sectors afterward.
Credential operations
reserve 0xffe000..0xffffff; preserve those sectors when backing up or replacing
firmware. See the
[native radio adapter](../../drivers/wifi/esp/s31/README.md) for asset
verification, ownership and radio qualification commands.


## Read-only SD probing

The `sd-test` profile uses SPI2 at 400 kHz from the 40 MHz crystal. Its pins
are GPIO24 SCK, GPIO25 MOSI, GPIO20 MISO and GPIO23 software CS. GPIO21/22
remain inputs with pull-ups; GPIO39 drives the active-low card power enable.
Opening the card claims these pads, cycles card power and sends the initial
clocks before CMD0. The SPI backend refuses an active dedicated SDIO pad
route. Close restores the saved clock and pin configuration and releases
its GPIO claims. Calls require serialized kernel foreground execution.

`tiku_s31_sdcard_open()` accepts 3.3 V SDHC/SDXC cards with CSD version 2.
It checks CMD8, OCR, CID and CSD and enables SPI CRC. Commands carry CRC7;
register and sector payloads require CRC16. Capacity uses a 64-bit sector
count, including cards with 2^32 sectors. `tiku_s31_sdcard_read()` checks the
LBA and copies a 512-byte sector to the caller after CRC validation.
Transfer failures leave caller data intact and require close and reopen.
The interface exposes no sector writes, erase operation or mount service.
Native four-bit SDMMC and its MPLL clock are outside this SPI backend.

Run the RAM tool with `--sd-test --seconds 8`. Success requires card
identification, partition/filesystem probing through `tiku_media_probe()`
and reads of the first and last sectors. The SPI checks hold card CS high
while routing MISO to the MOSI pad, exercise 1..257-byte transfers and
require timeout recovery and worker-context refusal.

`--allow-no-card-response` accepts a diagnostic run that passes SPI checks
but receives no CMD0 response. Its journal records `card_qualified: false`.
This result does not establish that the slot is empty or qualify card reads.
The default runner rejects it. The controller waits and card polling loops
have time and iteration bounds.


## Stereo audio

`tiku_audio_arch` uses I2S0 and AHB DMA pair one with eight private 1,024-byte
SRAM blocks per direction. It supports interleaved left/right signed PCM16
at 16 kHz. GPIO3 is BCLK, GPIO4 is WS, GPIO5 is codec input, GPIO6 is codec
output and GPIO7 enables the amplifier. The 40 MHz crystal and fractional
divider generate 512 kHz BCLK; the board has no MCLK wire. The backend enables
trusted DMA bus master 33 for the I2S request. Memory copies retain pair zero.

Open claims the pads, keeps the amplifier off and runs clocks. The caller
then opens `drivers/audio/es8389`, starts the audio rings and drains available
blocks through `tiku_s31_audio_poll()`. Each successful poll returns 256 stereo
frames and a sequence number. It copies caller playback into the released TX
block, eight blocks ahead of that capture sequence; NULL playback queues
silence. DMA never references caller buffers. Drain ready blocks before
yielding, keep pauses below 100 ms and sustain one poll per 16 ms.

Descriptor ownership prevents reuse of a block held by DMA. DMA errors,
I2S hangs, descriptor mismatches and a 200 ms progress timeout stop the
stream, disable the amplifier and latch failure. Close/reopen is required.
Status includes delivered blocks, RX/TX fault counts, timeout count and a
16 MHz start timestamp. Calls require serialized kernel foreground context;
workers and interrupt handlers are refused. Shutdown closes the codec while
clocks run, then closes the audio backend. DMA must park before buffers can
be reused; inability to stop it halts the CPU.

Build `audio-test` and run the RAM tool with `--audio-test --seconds 95`.
The profile checks changing loopback patterns across ring generations,
worker/interrupt refusals, queue exhaustion and reopen. It discards codec
startup transients, records 60 seconds of stereo PCM into a 3,840,000-byte
PSRAM allocation and compares the input hash with a reread hash. It exports
a one-second excerpt after stopping capture. The host checks the excerpt
checksum and writes a 16 kHz stereo WAV beside the log. Audio runs accept
80..120 seconds of capture time and one run per log.

The profile requires the optional ES8389 driver repository. It provides no
SD recording, compression, shell recording command or normal UI integration.
Its GPIO amplifier test does not establish acoustic playback, and digital
loopback does not establish microphone position or analog fidelity.

## Independent AP client qualification

The flash runner's `--ap-peer-test --runs 1 --peer-interface IFACE` uses
NetworkManager on Linux to associate a host Wi-Fi adapter with the S31's
WPA2 AP, then its SAE AP with required PMF. Each phase requires a DHCP
lease and eight ICMP replies from 192.168.4.1. The fixture profiles are
transient and have autoconnect and default routes disabled. The radio must
report the expected AP security and IPv4 service before host association.

Use a spare disconnected Wi-Fi adapter when available. Using a connected
adapter requires `--allow-uplink-interruption`; that adapter temporarily
leaves its current network. The runner saves its connection UUID and
restores and verifies it after each phase, including association, DHCP or
ICMP failure. A separate process restores the connection on parent exit
or after a 120-second deadline. Successful restoration removes the fixture
profiles. The test journal records the peer results and host restoration
alongside the board flash restoration. A failed host restoration retains
the transient profile for diagnosis.

The client requires SAE with PMF on the WPA3 leg. Successful association
and traffic test that admission policy; the fixture does not capture
management frames or qualify every PMF exchange and attack scenario.
