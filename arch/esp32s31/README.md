# ESP32-S31 Korvo-1 native port

The port runs native TikuOS on CPU0 of the ESP32-S31-Korvo-1, silicon revision
0.0. It builds for `rv32imafc_zicsr_zifencei` with the single-float ABI
`ilp32f`. CPU, memory, system and APB clocks use the 40 MHz crystal. HP1 is
stalled and LP execution is disabled. The linker uses 64 KiB of HP SRAM,
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
GPIO claims are implemented for GPIO0/1; GPIO58/59 are reserved for the
console and other pads report unreadable.

The flash shell boots through the installed Korvo second-stage bootloader.
It maps constants and an identity routine in two flash regions and loads
the kernel into SRAM. The PSRAM qualification profiles initialize and test
16 MiB of external memory. Persistent storage, display, SD, audio data
and wireless services are not implemented in these profiles. RAM loading
and warm-reset tests do not qualify true cold boot. The firmware profiles
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
| `shell` | Native shell with `help`, `ps`, `timer`, `queue`, `echo`, `calc`, `htimer` and `clear` |
| `flash-shell` | The shell with application metadata and two flash mappings for the factory bootloader |
| `psram-test` | Full-capacity patterns and physical cache-visibility checks with timer interrupts active |
| `flash-psram-test` | The PSRAM checks through the factory bootloader |

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

Use `--kernel-test`, `--shell-test`, `--watchdog-test` or `--psram-test`
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
requires reset before further PSRAM access. The backend has no allocator,
DMA client or power-management integration. Callers must establish those
ownership rules before handing buffers to peripherals.

The qualification test writes two complementary address-dependent patterns
across 16 MiB, invalidates after writeback and verifies the full span. It
also checks physical contents through SPI3, observing stale words before
writeback and updated words afterward. Run with `--psram-test --seconds 12`.
Use `flash-psram-test` and add `--psram-test --seconds 12` to the flash tool
to check initialization through the installed bootloader. Flash qualification
defaults to 15-second windows and rejects a second ROM boot within a window.

## Register sources

Register definitions and clock behavior were checked against ESP-IDF
[`4d59230`](https://github.com/espressif/esp-idf/tree/4d59230ddff16327812782151ef0afef202dc6d7):
S31 `hp_sys_clkrst_reg.h`, `interrupt_core0_reg.h`, `gpio_reg.h`,
`io_mux_reg.h`, `uart_reg.h`, `i2c_reg.h`, `timer_group_reg.h`,
`cpu_utility_ll.h`, `systimer_ll.h`, `i2c_ll.h` and `mwdt_ll.h`.
The board wiring is described in the
[Korvo-1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html).
The S31 radio archives use the same single-float ABI; they are not linked
into these profiles.
