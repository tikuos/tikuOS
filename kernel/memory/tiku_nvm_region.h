/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.h - the board's carved, memory-mapped NVM region (substrate B).
 *
 * A board-sized span of non-volatile memory carved by the linker
 * (__tiku_nvmfs_base / __tiku_nvmfs_size) straight out of the chip's FRAM /
 * MRAM / Flash -- distinct from the small SRAM-shadowed .uninit mirror.  It is
 * exposed as a single tiku_nvm_backend_t:
 *
 *   reads  : a plain pointer dereference into be->base (the region is
 *            memory-mapped and read in place -- zero SRAM shadow);
 *   writes : be->write(off, src, len), which must be issued inside a
 *            tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm() window.  The
 *            per-platform backend does the actual program (FRAM store in place
 *            / MRAM bootrom program / Flash erase+program).
 *
 * The NVM memory tier and the file store (tiku_tfs) ride this one region
 * without caring which technology backs it.  Parts with no carved region
 * (host, or a board where the feature is off) return NULL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_REGION_H_
#define TIKU_NVM_REGION_H_

#include "kernel/fs/tiku_nvm_backend.h"

/*
 * Region layout -- THREE extents, in address order, and nothing else:
 *
 *     [ tier | file store (/data) | reserved tail ]
 *      32 KB   absorbs the rest    named durable
 *
 * The NVM memory tier bump-allocates from the FRONT of the region; the top
 * TIKU_NVM_RESERVED_BYTES is held back for data that needs a STABLE address
 * across boots -- the tier never hands this tail out, so a consumer can own
 * a fixed offset in [size - reserved, size).  0 where the tier owns the
 * whole region (no carved tail).
 *
 * NAME IT HONESTLY: this is the BASIC DURABLE AREA, not a general facility.
 * BASIC is its only tenant (the saved program at the base, the run-state
 * checkpoint at the top); every size below is computed from BASIC's line
 * capacity; there is no registration API, so a second consumer would have
 * to hard-code an offset.  It is also reserved UNCONDITIONALLY -- a build
 * with BASIC compiled out still carries the carve, because /data's geometry
 * is compile-time (TIKU_TFS_MAX_FILES) and a moving tail would relocate
 * every file.  The reservation therefore protects /data from a statically
 * sized store, not BASIC from the allocator.
 *
 * This inverts layering (a core header sized by a shell feature) and is
 * slated for removal: once the store gains spanned files + streamed
 * replace, both slots become ordinary files and this constant disappears.
 * See temp/memlayout-fix-plan.md (P3b/P3c/P3g).
 *
 * The file store's extent is DERIVED (region - tier - reserved), not
 * hand-written, so the three extents always tile the region exactly.  Before
 * v0.06 the *tier* was the remainder and the FS was a hand-set number; that
 * put the leftovers in the one extent with no consumers (a live nRF54LM20
 * measured "tier pool 0 / 692224 peak 0" -- 676 KB idle).  Deriving the FS
 * instead makes idle space structurally impossible: whatever the code window
 * frees goes to files, which is the extent that can actually use it.
 *
 * Current tail tenants (kernel/shell/basic): the BASIC saved-program slot at
 * the tail BASE and the BASIC execution-state checkpoint slot at the tail TOP
 * (PERSIST / RUN RESUME); a _Static_assert next to the slot layout in
 * tiku_basic_ckpt.inl checks both fit.  Apollo510's tail is larger because its
 * HUGE-tier program slot (1700 lines, ~258 KB) plus the checkpoint slot
 * outgrew the shared 256 KB default.
 */
/*
 * Each size below is derived from the platform's BASIC capacity commitment:
 *
 *     reserved >= PROGRAM_LINES x 152 B   (saved-program slot, tail base)
 *              +  resume-snapshot slot     (vars + strings + arrays + pos)
 *
 * rounded up to a stable power-of-two-ish figure.  The numbers are
 * DELIBERATELY per-platform constants, not computed from the live BASIC
 * config: the region layout must not move when a build flag or a
 * -DTIKU_BASIC_PROGRAM_LINES override changes, or /data and the saved
 * program would silently relocate between builds of the same board.
 * The sync is guarded both ways: raising PROGRAM_LINES past a platform's
 * figure fails the _Static_assert in tiku_basic_ckpt.inl (save + ckpt
 * must fit), so these constants cannot silently under-provide.
 *
 * (The "+ ... snapshot" column is the REMAINDER the figure leaves for the
 * checkpoint slot, not the slot's own size -- the slot is ~16 KB, so every
 * platform carries slack.  Keep this table in step with PROGRAM_LINES in
 * tiku_basic_config.h; only the ckpt _Static_assert enforces the upper
 * bound, nothing catches a figure that has gone stale downward.)
 *
 *   apollo510  1700 x 152 = 258,400 + ~52 KB spare -> 320 KB
 *   ambiq      1024 x 152 = 155,648 + ~88 KB spare -> 256 KB
 *   lm20       1400 x 152 = 212,800 + ~32 KB spare -> 256 KB
 *   rp2350      512 x 152 =  77,824 + ~36 KB spare -> 128 KB
 *   l15         256 x 152 =  38,912 + ~10 KB spare ->  64 KB
 *   msp430     no tail: save/ckpt are .persistent FRAM arrays
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_RESERVED_BYTES  (320u * 1024u)   /* HUGE 1700-line + ckpt */
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_RESERVED_BYTES  (256u * 1024u)   /* BIG 1024-line + ckpt  */
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_RESERVED_BYTES  (128u * 1024u)   /* BIG 512-line + ckpt   */
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_RESERVED_BYTES  (256u * 1024u)   /* BIG 1024-line + ckpt  */
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_RESERVED_BYTES  (64u * 1024u)    /* BIG 256-line + ckpt   */
#else
#define TIKU_NVM_RESERVED_BYTES  0u
#endif

/*
 * TIER EXTENT -- one number, every platform.
 *
 * 32 KB of region-backed NVM scratch is the PORTABLE CONTRACT: code that asks
 * the NVM tier for <= 32 KB runs on every TikuOS platform, the same way the
 * 4 KB RP2350 durable window sets the ceiling for TIKU_DURABLE.  The figure is
 * set by the smallest member of the family -- MSP430, whose HIFRAM tier has
 * shipped at 32 KB since the tier was introduced (TIKU_TIER_HIFRAM_SIZE) and
 * cannot exceed 64 KB at all, because tiku_mem_arch_size_t is 16-bit there.
 *
 * What the extent is FOR (it is deliberately small -- see the ledger note in
 * the layout comment above):
 *   1. staging scratch for the machinery that manages the region itself --
 *      TFS shadow slots, the RP2350 multi-sector mirror rebuild, and the
 *      planned extent-header rewrite;
 *   2. headroom for a whole module image (32 KB == TIKU_MODULE_CARVE_SIZE), so
 *      a Tier-3 module arriving over a link could be staged in full before it
 *      is committed into the module slot.  NOTE: no code does this today --
 *      module images are compile-time blobs in .rodata and each backend stages
 *      privately (Nordic/MSP430 store straight through, Ambiq via a 256 B
 *      buffer, RP2350 via its own 4 KB static).  This is why the size was
 *      chosen, not a use that exists;
 *   3. the portable <= 32 KB allocation promise above.
 * It is NOT a durable heap for applications: an anonymous NVM allocation has
 * no name and no validity gate, so nothing can recover it after a reset.  Data
 * that must survive with its identity intact belongs in a FILE (/data) or in a
 * named TIKU_PERSIST_CELL -- both of which carry the gate discipline the raw
 * tier cannot.
 */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC)
#define TIKU_NVM_TIER_BYTES  (32u * 1024u)
#else
#define TIKU_NVM_TIER_BYTES  0u                /* no carved region */
#endif

/*
 * REGION SIZE -- the compile-time mirror of what arch/common/tiku_nvm_layout.ld
 * carves at link time:
 *
 *     region = layout_top - layout_code_cap - layout_module_size
 *                         - layout_durable_size
 *
 * The C side needs this as a constant because the file store's geometry (its
 * slot count, and on MSP430 its backing array) is fixed at compile time.  Keep
 * the two in step: if a device script changes one of the four inputs, change
 * the matching line here.  A mismatch is caught -- `df` prints the region size
 * the linker actually produced next to this expectation, and both the tier and
 * the /data mount refuse a region smaller than the three extents need.
 *
 *   apollo510  0x800000 - 0x488000 - 0x8000 - 0x10000 = 0x360000  3456 KB
 *   apollo4l/p 0x200000 - 0x090000 - 0x8000 - 0x10000 = 0x158000  1376 KB
 *   rp2350     0x400000 - 0x0F8000 - 0x8000 - 0x01000 = 0x2FF000  3068 KB
 *   lm20       0x1FD000 - 0x0C8000 - 0x8000 - 0x04000 = 0x129000  1188 KB
 *   l15        0x17D000 - 0x0C8000 - 0x8000 - 0x04000 = 0x0A9000   676 KB
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_REGION_BYTES  (3456u * 1024u)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_REGION_BYTES  (1376u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_REGION_BYTES  (3068u * 1024u)
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_REGION_BYTES  (1188u * 1024u)
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_REGION_BYTES  (676u * 1024u)
#else
#define TIKU_NVM_REGION_BYTES  0u
#endif

/*
 * FILE-STORE (TFS) EXTENT -- the remainder, by construction.
 *
 * Sits between the tier extent (front) and the reserved tail.  0 where the
 * file store rides its own backing (msp430 FRAM / host).  Resulting sizes:
 *
 *   apollo510  3456 - 32 - 320 = 3104 KB      lm20  1188 - 32 - 256 =  900 KB
 *   apollo4l/p 1376 - 32 - 256 = 1088 KB      l15    676 - 32 -  64 =  580 KB
 *   rp2350     3068 - 32 - 128 = 2908 KB
 *
 * TIKU_TFS_MAX_FILES (kernel/fs/tiku_tfs.h) is chosen to FILL its platform's
 * extent; the pair of static assertions in kernel/vfs/tree/tiku_vfs_tree_data.c
 * fails the build both if the store overflows the extent and if it leaves more
 * than one file's worth of it unused.
 */
#if TIKU_NVM_REGION_BYTES > 0u
#define TIKU_NVMFS_FS_BYTES                                                   \
    (TIKU_NVM_REGION_BYTES - TIKU_NVM_TIER_BYTES - TIKU_NVM_RESERVED_BYTES)
#else
#define TIKU_NVMFS_FS_BYTES  0u
#endif

/**
 * @brief Return the board's carved NVM region backend, or NULL if none.
 *
 * The returned backend is owned by the region layer (do not free).  Reads use
 * be->base directly; writes go through be->write inside an NVM unlock window.
 *
 * @return Pointer to the region backend, or NULL on parts without one.
 */
const tiku_nvm_backend_t *tiku_nvm_backend_get(void);

#endif /* TIKU_NVM_REGION_H_ */
