/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.h - the board's carved, memory-mapped NVM region.
 *
 * A linker-carved span of FRAM/MRAM/Flash exposed as one tiku_nvm_backend_t:
 * reads are a pointer dereference into be->base, writes go through be->write
 * inside an unlock window.  The NVM tier and the file store both ride it.
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
 * store owns everything above it.  That is the whole layout.  The rule that
 * keeps it that way:
 *
 *     Fixed extents are platform CONTRACTS.  Everything feature-shaped is a
 *     named file in one self-describing store.
 *
 * The tier extent is fixed because it IS a contract (32 KB on every part --
 * see below).  Nothing else qualifies: BASIC's saved program and run-state
 * checkpoint are ordinary files (prog.bas, prog.ckpt).  Spanned files and
 * streamed writes are what make that workable -- a file can exceed one slot and
 * be replaced in place without a RAM copy of the whole thing, so a fixed extent
 * buys nothing a name does not.
 *
 * The file store's extent is DERIVED (region - tier), not hand-written, so the
 * two extents always tile the region exactly.  Deriving the FS rather than the
 * tier makes idle space structurally impossible: whatever the code window frees
 * goes to files, the one extent that can actually spend it.
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
/*
 * Unconditional: this is the SIZE of the tier extent, not a claim that the
 * board has one.  Whether a region exists is answered at run time by
 * tiku_nvm_backend_get(), so the constant needs no platform list -- it is only
 * ever consulted on the path where a backend was already found.
 */
#define TIKU_NVM_TIER_BYTES  (32u * 1024u)

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
 * The code window is 0x60000 -- 384 KB -- on every part.  That is the point: a
 * code window is a contract about how much program a TikuOS image may be, not a
 * per-platform negotiation.  Measured largest images: 130.3 KB (apollo510b +
 * BLE), 121.6 KB (apollo4l/p), 122.6 KB (l15), 126.8 KB (lm20b + Axon driver),
 * 60.4 KB (rp2350) -- roughly 3x headroom everywhere.  Model weights and radio
 * firmware are store files, never .rodata, which is what keeps the window from
 * having to grow to fit a blob.
 *
 *   apollo510  0x800000 - 0x470000 - 0x0000 - 0x10000 = 0x380000  3584 KB
 *   apollo4l/p 0x200000 - 0x078000 - 0x8000 - 0x10000 = 0x170000  1472 KB
 *   rp2350     0x400000 - 0x060000 - 0x8000 - 0x01000 = 0x397000  3676 KB
 *   lm20       0x1FD000 - 0x060000 - 0x8000 - 0x04000 = 0x191000  1604 KB
 *   l15        0x17D000 - 0x060000 - 0x8000 - 0x04000 = 0x111000  1092 KB
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_REGION_BYTES  (3584u * 1024u)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_REGION_BYTES  (1472u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_REGION_BYTES  (3676u * 1024u)
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_REGION_BYTES  (1604u * 1024u)
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_REGION_BYTES  (1092u * 1024u)
#elif defined(PLATFORM_STM32N6)
/* Not linker-carved: the region is a span of the external NOR, so this mirrors
 * TIKU_XSPI_REGION_ADDR/BYTES in arch/stm32n6/tiku_xspi_arch.h rather than a
 * device script.  8 MB is what TIKU_TFS_MAX_SLOTS can address at a 4 KB slot. */
#define TIKU_NVM_REGION_BYTES  (8192u * 1024u)
#else
#define TIKU_NVM_REGION_BYTES  0u
#endif

/*
 * FILE-STORE (TFS) EXTENT -- the remainder, by construction.
 *
 * Everything in the region above the tier extent.  0 where the file store rides
 * its own backing (msp430 FRAM / host).  Resulting sizes:
 *
 *   apollo510  3584 - 32 = 3552 KB      lm20  1604 - 32 = 1572 KB
 *   apollo4l/p 1472 - 32 = 1440 KB      l15   1092 - 32 = 1060 KB
 *   rp2350     3676 - 32 = 3644 KB
 *
 * The store DERIVES its capacity from whichever of these the linker actually
 * carved -- there is no per-platform file count to keep in step any more, and
 * the /data mount takes rgn->size - tier rather than a constant, so a carve that
 * disagrees with the figures above yields more or fewer files instead of
 * silently losing the difference.  These numbers are documentation now.
 */
/**
 * @brief 1 on parts with a carved NVM region, 0 otherwise.  ALWAYS defined.
 *
 * Read this, never `TIKU_NVM_REGION_BYTES > 0`: an undefined identifier is 0 in
 * a preprocessor conditional, so the old spelling silently took the no-region
 * branch in any unit that forgot the include, and that branch is never an error.
 */
/* Names the EXCEPTIONS, not the members.  MSP430's FRAM is unified with the
 * code estate and host builds have no NVM at all; every other target carves a
 * region, so a new port is included by default rather than by being added
 * here.  A port with no backend yet still behaves: every consumer checks
 * tiku_nvm_backend_get() at run time, so being wrong here costs an error
 * rather than silent RAM. */
#if defined(PLATFORM_MSP430) || defined(TIKU_TEST_HOST)
#define TIKU_NVM_HAS_REGION  0
#else
#define TIKU_NVM_HAS_REGION  1
#endif

/*
 * FS extent, for the few callers that still want a compile-time figure.
 *
 * The /data mount and the NVM tier do NOT use this: they take the region size
 * the linker actually carved (rgn->size) and split it at the tier boundary, so
 * a carve that disagrees with the constants above cannot silently lose the
 * difference.  This remains for documentation and for sizing decisions that
 * genuinely must happen at compile time.
 */
#if TIKU_NVM_HAS_REGION
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
