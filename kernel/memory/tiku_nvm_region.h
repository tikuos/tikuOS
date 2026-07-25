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
 * Region layout -- TWO extents, in address order, and nothing else:
 *
 *     [ tier | file store (/data) ]
 *      32 KB   absorbs the rest
 *
 * The NVM memory tier bump-allocates from the FRONT of the region; the file
 * store owns everything above it.  That is the whole layout.
 *
 * There used to be a third extent -- a "reserved tail" at the top, held back
 * for data needing a STABLE address across boots.  It had exactly one tenant
 * (BASIC: the saved program at its base, the run-state checkpoint at its top),
 * every size was computed from BASIC's line capacity, and it was reserved
 * UNCONDITIONALLY -- a build with BASIC compiled out still carried the carve,
 * up to 320 KB of NVM that nothing could reach.  A core memory header sized by
 * a shell feature, with no registration API, so a second tenant would have had
 * to hard-code an offset.
 *
 * Both tenants are now ordinary files (prog.bas, prog.ckpt), which is what the
 * store gaining spanned files and streamed writes bought: a file can exceed one
 * slot and be replaced in place without a RAM copy of the whole thing, so a
 * fixed extent buys nothing a name does not.  The rule that replaced it:
 *
 *     Fixed extents are platform CONTRACTS.  Everything feature-shaped is a
 *     named file in one self-describing store.
 *
 * The tier extent stays fixed because it IS a contract (32 KB on every part --
 * see below).  Nothing else qualifies.
 *
 * The file store's extent is DERIVED (region - tier), not hand-written, so the
 * two extents always tile the region exactly.  Before v0.06 the *tier* was the
 * remainder and the FS was a hand-set number; that put the leftovers in the one
 * extent with no consumers (a live nRF54LM20 measured "tier pool 0 / 692224
 * peak 0" -- 676 KB idle).  Deriving the FS instead makes idle space
 * structurally impossible: whatever the code window frees goes to files, which
 * is the extent that can actually use it.
 */

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
 *      is committed.  NOTE: no code does this today -- a module image is a
 *      store file (mod.bin) that the store itself streams, and each install
 *      backend stages privately (Nordic/MSP430 store straight through, Ambiq
 *      via a 256 B buffer, RP2350 via its own 4 KB static).  This is why the
 *      size was chosen, not a use that exists;
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
 * the /data mount refuse a region smaller than the two extents need.
 *
 * apollo510's module_size is 0: its Tier-3 module image is a store file that
 * executes from the ITCM (tiku_basic_module.h), so the 32 KB executable slot
 * every other ARM part still carves became region on this one.
 *
 * The code_cap column is the SAME on every part -- 256 KB (P4).  That is the
 * point: a code window is a contract about how much program a TikuOS image may
 * be, and it stopped being negotiable per platform when the things that used to
 * inflate it (BASIC's durable tail, a module image counted twice, model weights
 * and radio firmware baked into .rodata) all became files.  Measured largest
 * images: 130.3 KB (apollo510b + BLE), 121.6 KB (apollo4l/p), 122.6 KB (l15),
 * 126.8 KB (lm20b + Axon driver), 60.4 KB (rp2350) -- so 256 KB is about 2x the
 * largest thing anyone has built, on every part.
 *
 *   apollo510  0x800000 - 0x450000 - 0x0000 - 0x10000 = 0x3A0000  3712 KB
 *   apollo4l/p 0x200000 - 0x058000 - 0x8000 - 0x10000 = 0x190000  1600 KB
 *   rp2350     0x400000 - 0x040000 - 0x8000 - 0x01000 = 0x3B7000  3804 KB
 *   lm20       0x1FD000 - 0x040000 - 0x8000 - 0x04000 = 0x1B1000  1732 KB
 *   l15        0x17D000 - 0x040000 - 0x8000 - 0x04000 = 0x131000  1220 KB
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_REGION_BYTES  (3712u * 1024u)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_REGION_BYTES  (1600u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_REGION_BYTES  (3804u * 1024u)
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_REGION_BYTES  (1732u * 1024u)
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_REGION_BYTES  (1220u * 1024u)
#else
#define TIKU_NVM_REGION_BYTES  0u
#endif

/*
 * FILE-STORE (TFS) EXTENT -- the remainder, by construction.
 *
 * Everything in the region above the tier extent.  0 where the file store rides
 * its own backing (msp430 FRAM / host).  Resulting sizes:
 *
 *   apollo510  3712 - 32 = 3680 KB      lm20  1732 - 32 = 1700 KB
 *   apollo4l/p 1600 - 32 = 1568 KB      l15   1220 - 32 = 1188 KB
 *   rp2350     3804 - 32 = 3772 KB
 *
 * TIKU_TFS_MAX_FILES (kernel/fs/tiku_tfs.h) is chosen to FILL its platform's
 * extent; the pair of static assertions in kernel/vfs/tree/tiku_vfs_tree_data.c
 * fails the build both if the store overflows the extent and if it leaves more
 * than one file's worth of it unused.
 */
#if TIKU_NVM_REGION_BYTES > 0u
#define TIKU_NVMFS_FS_BYTES  (TIKU_NVM_REGION_BYTES - TIKU_NVM_TIER_BYTES)
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
