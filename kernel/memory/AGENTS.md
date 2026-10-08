# kernel/memory/

All data is statically allocated; there is no heap.  Memory is tiered:
one API places an allocation in SRAM, HIFRAM (MSP430) or NVM by policy,
with a bump allocator per tier.  `tiku_tier_init()` is idempotent;
`tiku_tier_reset()` gives a clean slate.

## Durable placement

Never write a raw `__attribute__((section(".persistent")))` outside this
directory: per-file copies are how state has ended up silently volatile on
some platforms.  `tiku_mem.h` owns the grades:

- `TIKU_DURABLE`: survives a power cycle on every platform (FRAM/RRAM in
  place; SRAM with an NVM mirror on RP2350 and Ambiq).  Budgeted: it must
  fit the smallest target's link-asserted window (RP2350: 4 KB in all) or
  be platform-gated.
- `TIKU_RETAINED`: survives a warm reset; a power cycle may lose it.  For
  cross-reset diagnostics and tables rebuilt often (`.retained` SRAM on
  every port but MSP430, where it is FRAM behind the MPU window).
- `TIKU_FRAM_SPILL`: MSP430-only capacity spill for big session buffers
  (TLS, TCP, MQTT working state); not a durability claim, empty elsewhere.
- Bulk durable data goes through the carved NVM region: the NVM tier,
  `/data`, or a reserved-tail slot via `tiku_tier_nvm_write()`.

Small durable scalars use `TIKU_PERSIST_CELL` and the cell API
(`kernel/vfs/AGENTS.md`, "Persistent nodes").  Writes to `TIKU_DURABLE`
data sit inside `tiku_mpu_unlock_nvm()`/`tiku_mpu_lock_nvm()`; an
unbracketed write is silently dropped on MSP430, a bus fault on nRF54L and
a MemManage reset on RP2350 and Ambiq.  `tools/check_durable_placement.sh`
(`make lint`) rejects raw section attributes in `kernel/`, `interfaces/`,
`drivers/`, `boot/`, `hal/`, `apps/`, `services/`, `shell/` and `basic/`.

Ambiq's `.persistent` is physically SRAM mirrored to MRAM by a flush; it is
not a place for high-churn buffers.  RP2350 allocations that fall to the
NVM tier are flash and fault on write; `TIKU_TIER_SRAM_SIZE` and the
refuse-NVM-arena guard keep BASIC's arena in SRAM there.

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

The file store derives its capacity from the linker carve at mount; there
is no per-part file-count table.

## SRAM tier carve

The SRAM tier is carved by one linker fragment (`arch/common/`) on every
ARM part; the floor is single-authored on the make line
(`TIKU_TIER_SRAM_MIN` travels to the linker as `__tier_sram_floor`) and a
build whose span falls under it fails to link ("SRAM tier fell below its
floor").  Shrink the static buffer that grew, or lower the floor on purpose.

## Proof

Host: `make -C TikuBench/tests/host/kernel memory-layout` (every write cut,
recovery, replay) and the `memory-host` suite.  Board: the `filestore`
sweep after a fresh flash, `filestore --only refusal,layout
--allow-destructive`, `linkcarve`, `persist-cells` (MSP430).  Guide:
`TikuBench/docs/MEMORY_LAYOUT.md`.
