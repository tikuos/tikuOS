# kernel/memory/

All data is statically allocated; there is no heap.  Memory is tiered:
one API places an allocation in SRAM, HIFRAM (MSP430) or NVM by policy,
with a bump allocator per tier.  `tiku_tier_init()` is idempotent;
`tiku_tier_reset()` empties the tiers when no IPC backing pin or reclaim
fence/reset operation holds them; otherwise it returns `TIKU_MEM_ERR_BUSY`.

## Durable placement

Never write a raw `__attribute__((section(".persistent")))`: per-file
copies are how state has ended up silently volatile on some platforms.
`tiku_mem.h` owns the grades:

- `TIKU_DURABLE`: survives a power cycle on every platform (in place on
  MSP430 FRAM, nRF54L RRAM and RA8P1 MRAM; SRAM with an NVM mirror on Ambiq,
  RP2350, STM32N6 and the ESP32s).  Budgeted: it must fit the smallest
  target's link-asserted window (RP2350: 4 KB in all) or be platform-gated.
- `TIKU_RETAINED`: survives a warm reset; a power cycle may lose it.  For
  cross-reset diagnostics and tables rebuilt often (`.retained` SRAM on
  every port but MSP430, where it is FRAM behind the MPU window).
- `TIKU_FRAM_SPILL`: MSP430-only capacity spill for big session buffers
  (TLS, TCP, MQTT working state); not a durability claim, empty elsewhere.
- Bulk durable data goes through the carved NVM region: `/data`, or the
  NVM tier, whose memory is written with `tiku_tier_nvm_write()`.

Small durable scalars use `TIKU_PERSIST_CELL` and the cell API
(`kernel/vfs/AGENTS.md`, "Persistent nodes").  A value that changes on a
schedule or per event uses a ring: `TIKU_PERSIST_RING(ring, type, key, n)`
keeps it in `n` slots of sequence word, check word and value, a write goes
to the slot after the newest and a read takes the newest valid one, so a
cut leaves the previous value.  Where the medium has no erase unit and a
stated rating (RRAM, MRAM) the slots are 16-byte aligned and a write
programs one line; elsewhere a ring has two slots.  On Ambiq the relock
programs the whole mirror image for any change, so its slots spread no wear
there until the flush programs changed lines only.  `TIKU_PERSIST_COUNTER`
reserves a window of values in a ring before handing them out, one write
per window, and resumes above the reservation after a cut; the 802.15.4
frame counter uses it.  The slot array is a cell, so a change of its slot
count or value size re-primes it.  `/data` spreads its writes on the same
media (`TIKU_TFS_ROTATE`, `kernel/fs/tiku_tfs.h`): while two directory
entries are free, a replace moves the file to the first free entry and the
first free slot after its own, and a new file starts after the last one
placed, or after a mount past the highest live one; one-slot files stay
below the lowest longer file.  The new entry's
gate is the commit point, and a mount clears an old entry a cut left live.
Elsewhere a file takes the lowest free entry and slot.  Writes to `TIKU_DURABLE`
data sit inside `tiku_mpu_unlock_nvm()`/`tiku_mpu_lock_nvm()`; an
unbracketed write is silently dropped on MSP430, a bus fault on nRF54L, a
MemManage reset on RP2350, Ambiq and RA8P1, a store access fault that halts
on the ESP32-C5 and one the trap entry latches, counts and skips on the
ESP32-P4; STM32N6 and ESP32-C61 enforce nothing, and the write reaches the
mirror only when a later window closes (the table in `tiku_mpu.c`).  `tools/check_durable_placement.sh` (`make lint`) rejects raw
section attributes in `kernel/`, `interfaces/`, `drivers/`, `boot/`, `hal/`,
`apps/`, `services/`, `shell/` and `basic/`, except in `tiku_mem.h` and the
`mrambench` command; it does not scan `arch/`.

The C6 firmware profile with `C6_STORAGE=1` uses PMA entries 13 and 14 to
protect its durable SRAM span. An unbracketed store faults and enters the
native abort path. Its 8176-byte mirror image includes a fixed layout prefix
and a 4112-byte sector undo record. `TIKU_DURABLE_ARCH` places that port
recovery record at the linker-defined offset; application data uses
`TIKU_DURABLE` and persist cells.
Its retained SRAM survives ROM software reset within the same linked image;
EN-driven restart is excluded from that retention contract.

Ambiq's `.persistent` is physically SRAM mirrored to MRAM by a flush; it is
not a place for high-churn buffers.  RP2350 allocations that fall to the
NVM tier are flash and fault on write; `TIKU_TIER_SRAM_SIZE` and the
refuse-NVM-arena guard keep BASIC's arena in SRAM there.

## The medium

`tiku_nvm_media.h` holds the port's four facts, stated once in
`arch/<port>/tiku_nvm_media_arch.h`: `TIKU_NVM_MEDIA_UNIT`, the program
granule; `TIKU_NVM_MEDIA_TEAR`, what a power cut leaves in it (whole or
old, a byte prefix, or anything); `TIKU_NVM_MEDIA_ENDURANCE`, the programs
one unit is rated for, 0 where the medium states no limit; and
`TIKU_NVM_MEDIA_ERASE`, the bytes erased as one, 0 where a unit is
rewritten in place.  `tiku_nvm_media()` returns them as a struct and
`/sys/mem/media` prints them one a line.  Values, each header naming its
datasheet: RRAM 16 B, line, 10⁴; Apollo MRAM 16 B, line, 10⁵; RA8P1 MRAM
32 B, line, 10⁵; MSP430 FRAM 2 B, byte prefix, no limit; the flash ports a
256 B page, line, 10⁵, erased by 4 KB.  A host build with no platform macro
reads a 4-byte word that tears byte by byte, with no limit.  Derived from
the facts, not kept in platform tables: `TIKU_NVM_MEDIA_IN_PLACE`, which
`kernel/db` requires to build; the file store's slot and sector geometry
(`kernel/fs/tiku_tfs.h`); and `TIKU_PERSIST_WEAR_THRESHOLD`, half the
rating.  A write timing is not a fact here: the kernel observes it.

The pace of a write is the memory layer's too.  `tiku_nvm_pace_s()` gives
the shortest interval between writes that lets the medium last a number of
days, from the programs a write puts on its busiest unit and the places the
writes rotate over; `tiku_nvm_spread_for()` gives the places a declared
rate needs.  A value whose owner declares no life lasts
`TIKU_NVM_LIFE_DAYS`, ten years.  The database's capacity check
(`rate=`, `life=`) is `tiku_nvm_spread_for()`, and the file store reports
the pace of one file's replaces as the store stands (`tiku_tfs_pace_s()`):
BASIC waits that long between `PERSIST ON` checkpoints, over
`TIKU_BASIC_CKPT_LIFE_DAYS`, and at least `TIKU_BASIC_CKPT_INTERVAL_S`.

Values kept in SRAM reach NVM at sync points (`tiku_nvm_sync.h`).  Their
owner registers a hook with an interval: the boot record stores the
lifetime at its ring's pace (`tiku_persist_ring_pace_s()`) and the
write-back cache flushes its regions at the pace of one place, both over
`TIKU_NVM_SYNC_LIFE_DAYS`, twice the declared life, and no sooner than
`TIKU_NVM_SYNC_FLOOR_S`.  The scheduler polls the hooks on each pass of its
loop, so no timer is armed for them.  `reboot` runs every hook before the
reset, and `power off` before System OFF or a deep sleep.  `/sys/mem/sync`
lists the syncs, the last one's reason and each hook's interval.  Nothing
runs the hooks when the supply fails, so a power cut loses what changed
since the last sync.  MSP430 builds no sync points
(`TIKU_NVM_SYNC_ENABLE` is 0 there): its retained memory is FRAM, so the
lifetime a read notes survives a power cut.

A storage-free SRAM profile may define `TIKU_NVM_MEDIA_DISABLED`. It must
set `TIKU_MEM_NVM_ENABLE=0` and use zero program and erase geometry. Such a
profile has no writable medium; its linker rejects persistent and retained
sections. The S31 SRAM profiles use this descriptor.

`tiku_nvm_meter.h` observes it.  Each port charges the meter where it
programs the medium: the Nordic write HAL (one program for each 16-byte line
outside SRAM whose bytes differ, stored through the RRAMC write buffer and
committed in pieces of 16 lines), the Ambiq boot-ROM calls (every 16-byte
line of the span or mirror image), the RA8P1 copies into MRAM and the MSP430
copies (units touched), and the flash ports' erase and program routines
(pages and sectors).  The write HAL also writes `TIKU_RETAINED` data in
SRAM, such as the shell history, which programs nothing.  The persist
cells charge their direct word stores where `TIKU_DURABLE_IN_PLACE` is 1.
`/sys/mem/nvm_writes` prints bytes, programs, erases and `busy_us` since
boot.  Not charged: stores a caller makes into `TIKU_DURABLE` data in its
own unlock window without the HAL; time on a 16-bit timebase past one
counter period (65 ms at 1 MHz) in a single operation; writes before the
timebase runs, whose time reads as 0.

## NVM region layout

On the ARM ports the on-chip NVM is three estates: code, a carved region,
and a persist partition.  The region is split at run time between the NVM
tier and `/data` by the `nvm.tier` knob (`tiku_layout.c`; default 32 KB,
4 KB steps, ceiling leaving the store its `TIKU_TFS_MIN_SLOTS` floor).  A
change is staged in a persist-cell record (`layout stage nvm.tier=16K
--expect G:R --op N --erase`, or a write to `/sys/mem/layout/stage`) and
applied by `tiku_layout_boot()` from `tiku_tier_init()` before any
consumer allocates.  Rules that must hold:

- The mount never formats.  `/data` is created unasked only when the whole
  region is blank (every byte 0x00 or 0xFF, `tiku_tfs_may_provision()`);
  the first use records ownership with a TRNG-drawn identity.  A store
  found by scanning is never adopted: `layout inspect` lists candidates,
  `layout recover nvm.tier=<off> --accept-layout` names one, then a reboot.
  Anything else is held with a reason (`df` prints it) until recovery or
  `mkfs --erase-data`.
- `layout stage` must quote the record's identity and generation:revision,
  so a request from before a recovery cannot replay.
- A store move needs `--erase` when `/data` holds files.  The record says
  `rewriting` before the old header is retired, so a cut leaves the store
  held until `layout resume <op>`; nothing formats on its own after a cut.
- A held store publishes no NVM tier.  MSP430 has no knob (fixed arrays).
- With `TIKU_DB_ENABLE=1` a second knob, `nvm.db` (default 0, 4 KB steps),
  carves the database extent from the region's top; `/data` ends where it
  begins.  The record keeps schema 2, and its contract hashes the first
  knob only, so a record without `nvm.db` reads as 0 and an image without
  the knob holds a record that names it.  Any store change rewrites `/data`
  and formats the extent; tables count as loss beside files.  A lost record
  is not re-provisioned while an extent at the top holds tables; recovery
  names it: `layout recover nvm.tier=<off> nvm.db=<bytes> --accept-layout`.

The file store derives its capacity from the linker carve at mount; there
is no per-part file-count table.

## SRAM tier carve

`TIKU_MEM_NVM_ENABLE` defaults to 1. The C6 profiles set it to `C6_STORAGE`,
which defaults to zero and requires the firmware profile when enabled.
With it disabled,
the tier publishes no NVM backing, an NVM allocation is refused and
`tiku_tier_nvm_write()` returns `TIKU_MEM_ERR_NOT_FOUND`.

The SRAM tier is carved by one linker fragment (`arch/common/`) on every
ARM part; the floor is single-authored on the make line
(`TIKU_TIER_SRAM_MIN` travels to the linker as `__tier_sram_floor`) and a
build whose span falls under it fails to link ("SRAM tier fell below its
floor").  Shrink the static buffer that grew, or lower the floor on purpose.

## Proof

Host: `make -C TikuBench/tests/host/kernel memory-layout` (every write cut,
recovery, replay), `persist-ring` (programs per ring write, spread over the
slots, a cut after every byte, the ring's pace, on four media), `nvm-sync`
(each hook at its own interval), `tfs-wear` (the worst line
over a thousand `/data` replaces and creates, on three media) and the
`memory-host` suite.  Board: the `filestore`
sweep after a fresh flash, `filestore --only refusal,layout
--allow-destructive`, `linkcarve`, `persist-cells` (MSP430).  Guide:
`TikuBench/docs/MEMORY_LAYOUT.md`.
