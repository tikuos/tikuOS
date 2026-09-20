/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.h - the board's carved, memory-mapped NVM region.
 *
 * A carved span read through be->base and written through be->write in an
 * unlock window. ARM tiers and stores use it; MSP430 keeps separate arrays.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_REGION_H_
#define TIKU_NVM_REGION_H_

#include "kernel/fs/tiku_nvm_backend.h"

/*
 * Region-backed ARM ports reserve 32 KB for the NVM tier and give the rest
 * to /data. The backend's actual size determines whether that split fits.
 * MSP430 instead has a separate lower-FRAM tier and static /data array;
 * neither uses its pinned region backend. HIFRAM is a different tier.
 * Anonymous tier allocations have no recovery identity: use a file or cell
 * for named persistent data.
 */
#define TIKU_NVM_TIER_BYTES  (32u * 1024u)

/*
 * REGION SIZE -- the compile-time mirror of what arch/common/tiku_nvm_layout.ld
 * carves at link time:
 *
 *     region = layout_top - layout_code_cap - layout_persist_size
 *
 * The C side needs this as a constant because the file store's geometry (its
 * slot count, and on MSP430 its backing array) is fixed at compile time.  Keep
 * the two in step: if a device script changes one of the three inputs, change
 * the matching line here.  A mismatch is caught -- `df` prints the region size
 * the linker actually produced next to this expectation, and both the tier and
 * the /data mount refuse a region smaller than the two extents need.
 *
 * The code window is 0x60000 -- 384 KB -- on every part.  That is the point: a
 * code window is a contract about how much program a TikuOS image may be, not a
 * per-platform negotiation.  Measured largest images: 130.3 KB (apollo510b +
 * BLE), 121.6 KB (apollo4l/p), 122.6 KB (l15), 126.8 KB (lm20b + Axon driver),
 * 60.4 KB (rp2350) -- roughly 3x headroom everywhere.  Model weights and radio
 * firmware are store files, never .rodata, which is what keeps the window from
 * having to grow to fit a blob.  The Tier-3 module slot is INSIDE that window
 * (its top 32 KB, reserved only in loader builds), not an estate of its own,
 * so it appears in none of the arithmetic here.
 *
 *   apollo510  0x800000 - 0x470000 - 0x4000 = 0x38C000  3632 KB
 *   apollo4l/p 0x200000 - 0x078000 - 0x4000 = 0x184000  1552 KB
 *   rp2350     0x400000 - 0x060000 - 0x01000 = 0x39F000  3708 KB
 *   lm20       0x1FD000 - 0x060000 - 0x04000 = 0x199000  1636 KB
 *   l15        0x17D000 - 0x060000 - 0x04000 = 0x119000  1124 KB
 *   ra8p1      0x100000 - 0x060000 - 0x04000 = 0x09C000   624 KB
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_REGION_BYTES  (3632u * 1024u)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_REGION_BYTES  (1552u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_REGION_BYTES  (3708u * 1024u)
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_REGION_BYTES  (1636u * 1024u)
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_REGION_BYTES  (1124u * 1024u)
#elif defined(PLATFORM_RA8P1)
#define TIKU_NVM_REGION_BYTES  (624u * 1024u)
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
 *   apollo510  3632 - 32 = 3600 KB      lm20   1636 - 32 = 1604 KB
 *   apollo4l/p 1552 - 32 = 1520 KB      l15    1124 - 32 = 1092 KB
 *   rp2350     3708 - 32 = 3676 KB      ra8p1   624 - 32 =  592 KB
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
