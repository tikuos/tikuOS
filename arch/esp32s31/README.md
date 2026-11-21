# ESP32-S31 Korvo-1 native port

The port runs native TikuOS on CPU0 of the ESP32-S31-Korvo-1, silicon revision
0.0. It builds for `rv32imafc_zicsr_zifencei` with the single-float ABI
`ilp32f`. CPU, memory, system and APB clocks initially use the 40 MHz crystal. HP1 is
stalled and LP execution is disabled. The kernel-test linker uses 64 KiB of HP SRAM,
including a 4 KiB kernel stack; it rejects persistent and retained sections.

`S31_CPU_MHZ` defaults to 40. Flash firmware profiles also accept 160 with
`S31_PSRAM_MHZ=40` or `80`. After PSRAM initializes CPLL, the runtime selects
160 MHz CPU/memory, 80 MHz system and 40 MHz APB clocks. It verifies PLL
configuration, bounds the clock-update wait, reads back the dividers and
updates the ROM delay calibration. A refused transition stops memory
initialization. Timer, UART and display clock sources retain their configured
rates. Frequency control reports the selected fixed CPU rate; runtime changes
are refused.

The runtime provides non-nested CLIC interrupts, a 128 Hz kernel clock,
a 1 MHz hardware timer, processes, preemptible workers, integer/FPU context
preservation, per-context C-library state and a timer-group watchdog.
Startup disables the bootloader LP watchdog and its flash-boot mode before
the application selects its watchdog policy.
PIE and hardware-loop context are not supported. Faults print their cause
PC and fault address through UART0 and halt with machine interrupts masked.

UART0 uses GPIO58/59 at 115200 baud, 8N1, with an interrupt-fed receive queue
holding 1,023 bytes. I2C0 uses the Korvo's pull-ups on SDA GPIO0 and SCL GPIO1
at configured rates of 100/400 kHz. Transfers preserve repeated START across
FIFO batches and use bounded waits. The receive batches hold at most 31
bytes so the final NACK can complete. Closing the bus restores its pin state.
GPIO claims cover GPIO0/1, audio GPIO3..7, the 21 RGB-panel pads and card GPIO20..25/39. GPIO58/59 are reserved
for the console. The firmware namespace provides read-only GPIO inspection;
raw pad reconfiguration is refused.

The flash shell boots through the installed Korvo second-stage bootloader.
It maps constants and an identity routine in two flash regions and loads
the kernel into SRAM. The shell attaches PSRAM to the kernel memory tiers. The memory
qualification profiles test allocation and AHB DMA transfers; the PSRAM
profiles test the full device capacity. Display profiles exercise RGB scanout
and touch polling. The SD profile exercises SPI2 and read-only SDHC/SDXC probing.
The audio profile exercises stereo I2S and ES8389 capture into PSRAM.
The `firmware` profile adds native station/AP Wi-Fi, DHCP, shell networking
encrypted credentials in a reserved 8 KiB flash region, durable `/data`, VFS
and BASIC. RAM loading
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
| `trng-test` | LP entropy-source health soak in SRAM, with passive monitoring or bounded random reads |
| `shell` | SRAM and PSRAM allocation, native shell with `help`, `ps`, `timer`, `queue`, `echo`, `calc`, `htimer` and `clear` |
| `firmware` | Durable `/data`, VFS, BASIC, native station/AP Wi-Fi, SAE/PMF, DHCP and encrypted profiles |
| `s31-local` | The application named by `S31_APPLICATION_MANIFEST` with the shell, VFS and BASIC in the firmware flash layout, without radio |
| `s31-connected` | The `s31-local` contents plus Wi-Fi, started after the application |
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

`--application-test /path/to/provider.py` selects a local Python qualification
provider for a firmware application. The module exports
`ApplicationQualification`: `project` is the descriptor name as bytes;
`capture(wire, seconds, run)` returns console bytes and a secondary result;
`passed()` reports the latest run; `results` holds JSON-serializable records;
`console` retains raw bytes, including a failed capture. The runner records the
provider's source hash and requires matching image and boot project names,
successful application startup and one boot without a fatal marker.

Application mode protects the general storage reservation and the settings
tail as well as application sectors. Both reservations must be erased in the
backup and on the board. The runner restores and verifies these regions after
qualification or failure. Select this mode without `--firmware-test` or
`--settings-test`; it enables both protections itself. Provider execution uses
the runner's already-open UART. Board access requires an explicit request.

## PSRAM ownership

The PSRAM backend initializes the Korvo's AP octal 16 MiB device with its
1.8 V supply. `S31_PSRAM_MHZ` selects a 20, 40 or 80 MHz bus. Display
profiles select 80 MHz; other profiles default to 20 MHz. The 20 MHz setting
uses the 40 MHz crystal. The 40/80 MHz settings calibrate the 320 MHz CPLL
and divide it for the mapped controller; PIO commands remain at 20 MHz.
With `S31_CPU_MHZ=40`, CPU, system and APB clocks remain at 40 MHz. CPLL configuration belongs to
boot initialization and must not overlap another PLL user. The backend verifies
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
between the linked statics and the kernel stack. Firmware reserves a 16 KiB
stack; the SRAM shell reserves 4 KiB. Repeated calls preserve
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

`tiku_s31_dma_fill()` writes a repeated 32-bit word into an owned span with
the same size, alignment and context requirements. Each TX descriptor reads
a 4,032-byte SRAM pattern; RX descriptors advance through the destination.
The complete overwrite discards destination cache lines before DMA and
invalidates them after DMA. Dirty data outside the requested lines is retained.
Copy and fill share the pair-zero reservation and failure handling.

The AHB bus master uses TEE master 48 in trusted mode; initialization refuses
a locked incompatible mode. Memory-region filters remain enabled. This
privileged driver does not provide DMA isolation for untrusted callers.
The transfer path polls completion with timer interrupts enabled and does not
provide asynchronous peripheral queues.

Run `memory-test` with `--memory-test --seconds 30`, or use
`flash-memory-test` and the same options with the flash runner. Its workload
covers four source/destination memory combinations, descriptor boundaries,
1 MiB transfers, canaries, worker/interrupt refusal, descriptor errors,
timeout recovery and 32 repeated 800×480 RGB565-sized transfers. Fill tests
cover SRAM and PSRAM, dirty guard lines, batch boundaries and fault recovery.
The test verifies
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

The pixel clock is 16 MHz from the crystal with a fractional divider of
2.5. Horizontal sync/back/front intervals are 40/40/48 clocks; vertical
intervals are 23/32/13 lines. The 928×548 total frame gives 31.46 Hz scanout.
The backend requires the 80 MHz PSRAM setting. GPIO38 has no brightness
control through this backend.

The caller retains its framebuffers and display structure until
`tiku_s31_display_close()`. Initialization reserves a 768,000-byte PSRAM
scanout surface and two rings of 191 SRAM descriptors, 6,112 bytes in total.
Full-width rectangles and clears use AHB DMA fills. Other rectangles use
paired RGB565 word stores with halfword edges and eight-word groups.
In single-buffer mode, drawing writes the caller's buffer. Present stops scanout and copies the cache-line-expanded span from the first
damaged pixel to the last through AHB DMA, including intervening rows. It
restarts at the ring's beginning and waits for two frame boundaries with a
300 ms deadline. Single-buffer updates interrupt refresh.

The backend advertises `TIKU_DISPLAY_CAP_FLIP`. Use
`tiku_display_set_buffers()` with two disjoint, 64-byte-aligned, complete
PSRAM surfaces. Drawing targets the back buffer; flushing records no
scanout change. `tiku_display_flip()` publishes the back buffer's cache lines
in 4,096-byte batches and redirects the current descriptor ring at its frame
boundary. It requests the AXI DMA link-switch event and verifies that the
current descriptor belongs to the new ring before returning the old front
buffer for drawing. The old frame's final pixels can remain in the LCD
pipeline after ownership returns. Scanout stays active during publication
and handoff.
The wait has a 500 ms deadline; a failed handoff stops DMA and requires close
and reopen. Calls that draw into the current scanned surface are refused.
Direct caller writes must obey the same ownership rule. The private surface
remains reserved until close, including in paired mode. Circle and
rounded-rectangle acceleration are absent.

The backend also advertises `TIKU_DISPLAY_CAP_ASYNC`. With a paired display,
`tiku_display_submit()` retains the buffers under a nonzero, increasing token.
Keep the display structure and both buffers unchanged until
`tiku_display_poll()` returns a terminal result for that token. Tokens remain
increasing across close and reopen. Pixels outside the submitted rectangle
must already be correct in the back buffer.

Each publication poll writes back at most 4,096 bytes. Dirty columns expand
to 64-byte cache lines within each dirty row; full-stride rows coalesce into
one span. Later polls request handoff and check both the switch event and
the current descriptor's membership in the new ring. Completion swaps the
public front/back pointers. Synchronous drawing and flipping are refused
while a submission is pending. Existing synchronous clients retain their API.

Publication errors and a 500 ms handoff timeout begin polled retirement.
Buffers remain retained until both TX and its link report stopped.
`TIKU_DISPLAY_ERR_RETIRED` reports that failure with stopped DMA and requires
close and reopen. Other negative poll results refuse the call without
changing ownership. Failure to prove retirement within 500 ms
halts execution with storage retained. Close retains its synchronous stop
contract. Batch size limits work per poll; board timing remains a separate
qualification requirement.

GPIO33/34 carry RGB data and also connect to the USB Serial/JTAG PHY.
Initialization disables that PHY's pads and overrides its pull resistors
while LCD owns the pins. Close restores the saved USB pad configuration.
The UART0 console uses GPIO58/59.

An application profile links paired presentation when its manifest adds the
display sources. The manifest's linker script places the application's
display code in IROM while DMA descriptors remain in SRAM. Touch is selected
separately. Physical timing and resource qualification are separate from
target-link checks.

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
this backend in the display profiles. The AXI internal-memory window covers
the complete descriptor array at its linked SRAM address; PSRAM pixels use
the separately configured external-memory window. Camera sharing and general DMA-channel
allocation are absent.

Run `display-test` with `--display-test --seconds 35`. The test requires the
optional Goodix driver checkout and exercises 100 Hz touch polling during a
ten-second scanout interval. It reads VSYNC, enable and RGB data through
GPIO inputs, verifies copied framebuffer contents with scanout stopped,
and checks 16 paired solid-color frames with touch polls between flips.
The new color must appear within 33 ms of a handoff, followed by 64 matching
active RGB samples. Each GPIO snapshot masks interrupts and must finish
within the eight-microsecond horizontal blanking interval. A 1 kHz timer
samples VSYNC across blocking handoffs.
The test reports fill, flip and complete-redraw timings; its complete-redraw
mean must remain within 33,333 microseconds. Tests also cover odd/even
rectangle edges, zero width, full-width row guards, clipped drawing,
full-screen clear, ownership conflicts, worker/interrupt
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
the runner restores the factory application and storage reservation afterward. This smoke test does
not write settings or establish client authentication. Add `--settings-test --runs 2` to save a fixture profile, verify locked reboot and wrong-key
refusal, unlock and forget it. This mode requires erased settings sectors
matching the backup and restores and verifies those sectors afterward.
Credential operations
reserve 0xffe000..0xffffff; preserve those sectors when backing up or replacing
firmware. See the
[native radio adapter](../../drivers/wifi/esp/s31/README.md) for asset
verification, ownership and radio qualification commands.


## Durable storage and desktop services

The `firmware` profile provides the shared VFS, Tiku File Store and BASIC.
It reserves these physical flash ranges:

| Range | Use |
| --- | --- |
| `0xe40000..0xfeffff` | 1,728 KiB region: a 32 KiB NVM tier and the `/data` extent |
| `0xff0000..0xff3fff` | First 16 KiB durable-SRAM mirror |
| `0xff4000..0xff7fff` | Second 16 KiB durable-SRAM mirror |
| `0xffe000..0xffffff` | Encrypted radio profiles, owned by the settings backend |

The factory partitions and the additional image at `0xe10000` remain outside
these reservations. Startup rejects a partition overlapping general storage.
The first `0x1b8000` bytes of PSRAM cache the region and mirrors and are
excluded from allocation and DMA backing. The remaining `0xe48000` bytes
belong to the PSRAM tier. BASIC permits its arena in that tier.

PMA entries 1 and 2 guard the durable SRAM span, with 4 KiB-aligned bounds.
`tiku_mpu_unlock_nvm()` opens its write window; relock commits changed state
to the alternate flash mirror and restores read-only access. A store outside
the window faults. The mirror checksum covers generation, length and data;
the commit word is programmed last. The fixed recovery record occupies
4,112 bytes at durable offset 256; ordinary durable objects start at offset
8,192. The linker bounds the complete durable span to 16,368 bytes.

Region writes preserve the old sector in that recovery record before erase
and clear the record after verified programming. Boot restores a pending
sector before mounting the store. Writes are synchronous, require kernel
foreground context and mask interrupts around ROM flash operations. Cache-
disabled code and staging buffers reside in SRAM. Unknown nonblank media
is held by the shared layout service; it is not automatically formatted.
Flash endurance is reported as unspecified. The undo journal and complete
mirror writes add erase/program work to each changed sector.

The default store admits 419 file entries. These are TikuOS shell commands:

```text
write /data/hello.txt hello
cat /data/hello.txt
df
basic
10 PRINT 6 * 7
SAVE
BYE
reboot
basic
LOAD
RUN
```

`recv` and `send` transfer binary files using the shell's length and chunk
handshake. `/data/basic` is the BASIC program bridge. The desktop storage
kit mounts the serial namespace and supports file values and BASIC
execution through its existing board backend. The optional window/BASIC
channel is selected with `TIKU_APPL_GUI=1`; it requires the applications
overlay. `S31_GUI_KITS=/path/to/Stomme/kits` selects the shared kit sources
when the overlay's kits have moved to a separate checkout. With the overlay
absent, a build that leaves application flags unset has no application sources.

For service qualification, build `S31_PROFILE=firmware S31_SERVICES_TEST=1`
and run `tools/esp32s31_flash.py --services-test --runs 2` with the same
port, serial, image, backup, SHA256 and log arguments as flash qualification.
The probe requires a guarded store fault and an unlocked store to complete.
The runner creates a file, saves and runs BASIC, transfers an 8,193-byte
binary file, and verifies them across EN and software resets. Retained SRAM
is checked across software reset within the same linked image; EN restart
and power loss are excluded from its retention contract.

`--desktop-test /path/to/executable` runs an opt-in host test with `-p PORT`
after releasing the serial handle, then reopens it for restoration. It can
be repeated for the desktop's `test_boardbasic` and TikuBench's host-side
`tests/esp32s31/desktop_services.c`, linked against the desktop storage,
device, support and application libraries. A skipped host test fails the run.
Every firmware qualification requires an erased general-storage reservation
matching the backup and restores and verifies it afterward. The runner
records restoration status before and after mutation.


## SD card and media storage

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
After identification, `tiku_s31_sdcard_clock()` selects 400 kHz..20 MHz,
rounding down to a supported divider of the crystal clock. Writes require
`tiku_s31_sdcard_write_enable()` to select an extent within the identified
card. Each single-sector write sends CRC16, checks the data-response token,
waits for programming completion and checks both CMD13 status bytes. The
sync operation completes prior programming; no optional card cache is
enabled. An I/O failure freezes transfers until close and reopen. Close
retains ownership when pending programming cannot be quiesced.
Native four-bit SDMMC and its MPLL clock are outside this SPI backend.

`TIKU_S31_SD_ASYNC_ENABLE=1` adds caller-owned open, clock and read operations.
`tiku_s31_sd_open_begin()` claims the card and starts SPI configuration;
`tiku_s31_sd_poll()` advances one phase without a hardware wait or delay.
The two power intervals use elapsed-clock checks. Command responses and data
tokens advance through polled FIFO requests of at most 64 bytes. CRC checks
precede copying a sector to the caller. Successful operations retain the open
card for another operation. Errors close the read-only session; invalid-call
refusals retain ownership. `tiku_s31_sd_cancel()` resets a pending SPI transfer
and releases the pads before releasing borrowed buffers. Synchronous card
operations are refused while an asynchronous operation owns the transport.

`tiku_s31_mfs_async_acquire/read/poll/close` bind that transport to the observed
CID and an explicitly selected block extent. Acquisition verifies card identity
and capacity before selecting the requested clock. Reads accept 1..8 sectors,
retain the session across calls, and never enable a write extent. Caller output
can contain completed sectors if a later sector fails. The caller serializes
the session with filesystem work and supplies its state storage. The existing
queued store and block API remain synchronous; their clients must use a
resumable filesystem operation to benefit from the asynchronous transport.
The feature defaults off. An application manifest can enable it;
host transport fixtures do not establish board timing or service throughput.

The firmware profile enables `S31_MEDIA_ENABLE=1` by default. Set it to
zero to omit the card service. The S31 block adapter validates CID and the
selected extent each time it acquires the card. Completed sector transfers
feed the watchdog during synchronous media requests; bounded error waits
do not feed it. The shared media service
allocates its two volume bitmaps in PSRAM and mounts the native filesystem
without formatting. In the TikuOS shell:

```text
media mount 20000000
media info
ls /media
cat /media/notes/hello.txt
```

`media mount` defaults to 4 MHz. `media begin`, `append`, `sync`, `finish`,
`release`, `resume` and `rm` use the shared serialized media service.
The VFS view supports listing and reads; mutation uses the media commands
or transfer protocol. Formatting remains an explicit CID/UUID-bound
operation. The media transfer staging arena also uses PSRAM on S31.

`S31_PROFILE=media-test` mounts the existing volume at 20 MHz and reads
complete objects, comparing SHA-256 against their catalog digests. Its
default is read-only. An explicit `S31_MEDIA_TEST_HEADER` can define
`TIKU_S31_MEDIA_TEST_PHASE` as 1 to create a 131,089-byte test object or 2
to verify and delete it after another boot. That header must bind
`TIKU_S31_MEDIA_TEST_CID` to four least-significant-word-first SD register
words; `TIKU_S31_MEDIA_TEST_NAME` selects the test name. Creation refuses
an occupied name. Both write phases audit the other complete files before
and after the operation. Creation exercises synced-prefix release, remount
and resume. The runner requires `--media-test --media-phase 1` or `2` to
match a write image; phase zero is read-only. Captures can use
`--seconds 240` for complete-file audits. The profile uses 128 KiB of low
SRAM and PSRAM buffers; it does not initialize radios.

Computer transfers use the UART bridge identity:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyUSB0 \
    --bridge-serial BRIDGE_SERIAL ls
```

Replace `BRIDGE_SERIAL` with the CP2102N serial from host enumeration.
The tool opens at 115200 baud with DTR and RTS asserted together and checks
port ownership. This avoids a sequential deassertion pulse on the board reset circuit.
The bridge transport uses 256-byte payloads to bound escaped UART frames. Mount from the TikuOS shell first, then close that terminal.
`put` and `get` share the protocol documented in
[computer transfers](../../services/storage/media-transfer.md).
The firmware profile enables TCP with receive and retransmission buffers in
SRAM. Media staging remains in PSRAM. Filesystem, network and radio-control
foreground code executes from flash to leave SRAM for the shared radio heap.
After Wi-Fi association and DHCP, provision a listener from the computer:

```sh
python3 tools/tiku_media_transfer.py --serial /dev/ttyUSB0 \
    --bridge-serial BRIDGE_SERIAL --key-file media.key serve --port 7331
python3 tools/tiku_media_transfer.py --tcp BOARD_IP:7331 \
    --key-file media.key put recording.opus audio/recording.opus
python3 tools/tiku_media_transfer.py --tcp BOARD_IP:7331 \
    --key-file media.key get audio/recording.opus downloaded.opus
```

Replace `BOARD_IP` with the address reported by `ip` in the TikuOS shell.
`media off` stops the listener. The access key and files travel over plain
TCP; use a secured, trusted Wi-Fi network. Listener keys reside in RAM and
must be provisioned again after reboot.

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

## Optional native BLE

The normal firmware accepts `S31_BLE_ENABLE=1` with the pinned external
SDK and Bluetooth controller assets described in the
[radio adapter contract](../../drivers/wifi/esp/s31/README.md#ble-controller-and-host).
This enables the `bt` shell commands, central/peripheral GATT access,
1M/2M/Coded PHYs and LE Secure Connections. The default build gives the PHY
to one radio at a time; `wifi off` releases it for `bt on`, and `bt off`
releases it for Wi-Fi. Add `S31_RADIO_COEX=1` to share an 88 KiB SRAM radio
heap and the SDK coexistence arbiter. Each radio retains ownership through
shutdown; the final owner restores shared clocks and DMA permissions.
The controller archives are supplied separately.

Software ECDH services native controller workers during scalar multiplication
to maintain connections with short supervision timeouts. The host enables
Encryption Key Refresh Complete events so a refreshed link releases its
pairing state before another peer starts pairing. Hardware entropy health
faults remain latched until reset; RF-load tests have observed such faults.
See the radio adapter contract for the per-mode qualification limits.

## Local application manifest

The `s31-local` and `s31-connected` profiles build an application kept
outside this tree. `S31_APPLICATION_MANIFEST` names its make fragment and has
no default for these profiles. The manifest sets `APPL_SRCS`, `APPL_CFLAGS`
and `APPL_LINKER`, the linker script for the image. `APPL_EXTRA_OBJS` adds
objects built by the manifest's own rules, `APPL_LINK_DEPS` link
prerequisites and `APPL_CHECK_ELF` a command run on the linked ELF.
`APPL_CFLAGS` defines
`TIKU_APPLICATION_HEADER`, the header that declares `tiku_application_start()`
and `tiku_application_vfs_get()`, and `TIKU_APPLICATION_PROJECT`, the project
name in the image descriptor. The profiles use 80 MHz PSRAM and the firmware
flash layout. `S31_UI_LTO` is refused outside them and read by the manifest.

These profiles select no LCD or touch driver of their own; the manifest adds
the drivers its application uses. The connected profile starts its
network process after application startup. The `firmware` profile keeps
`applications/applications.mk` as its default application manifest.

A manifest may place zero-initialized statics in a section ending at the top
of PSRAM and define `__tiku_s31_psram_statics_start` at its start; the PSRAM
tier is then attached below that address.
An explicitly selected missing manifest fails configuration.


## Polled region writes

`TIKU_S31_STORAGE_ASYNC_ENABLE=1` adds the caller-owned operations in
`tiku_storage_async.h`. The default is zero. Begin accepts a mapped region
view and immutable input, requires an established durable mirror and reserves
region writes and mirror flushes. Calls run in the serialized kernel owner.
Pending operations retain their context and input until poll reports terminal
completion; a caller cancellation must drain accepted work.

The writer advances undo copying and CRC work in 256-byte pieces, programs
at most 64 bytes within a page, verifies the program on a later poll and
commits the mirror magic after its body. Region shadow publication follows
the durable mirror that clears the undo record. Boot uses the existing mirror
selection and undo replay. A backend failure latches storage unavailable until
boot recovery. The durable layout is unchanged.

The radio flash linker exports `__tiku_s31_irom_start` and
`__tiku_s31_irom_end` around the linked code segment. The driver option
`TIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE` checks station code against that mapped
segment; it is disabled by default.

The flash implementation checks JEDEC ID `46 40 18` and the SFDP basic-parameter
suspend profile. SPI1 programs and erases with automatic suspension for SPI0
cached reads. Automatic resume is disabled; the storage owner resumes a suspended
operation on its next poll. It uses a command clock of at most 20 MHz, saves the
controller configuration and restores it after retirement. Admission limits
the SFDP program/erase resume intervals to 64/512 us and suspend latency to
50 us. At 20 MHz the controller waits 64 us after program resume, 512 us after
erase resume and 51.2 us after suspend; slower clocks lengthen these delays.
Short command and
progress deadlines halt on failure. Cached reads must stay outside the region
being programmed or erased; DMA reads from flash during these operations are
not part of this contract. Product code, constants and transaction state may
remain in mapped memory. The synchronous storage busy check remains resident. Both general-storage and
encrypted Wi-Fi-settings ROM access refuse a pending automatic operation before
changing caches.

Program/erase submissions charge the NVM operation counters once. Their busy
time sample covers submission, excluding pending wall time and cache arbitration.
Target timing, concurrent service behavior and physical flash interruption
require qualification on the selected image and board.
