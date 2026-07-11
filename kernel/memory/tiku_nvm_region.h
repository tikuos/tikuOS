/*
 * Tiku Operating System v0.05
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
 * Region layout. The NVM memory tier bump-allocates from the FRONT of the
 * region; the top TIKU_NVM_RESERVED_BYTES is held back for durable NAMED data
 * that needs a STABLE location across boots (the BASIC saved program today, the
 * file store later) -- the tier never hands this tail out, so a named consumer
 * can own a fixed offset in [size - reserved, size). 0 where the tier owns the
 * whole region (no carved tail).
 */
#if defined(PLATFORM_AMBIQ)
#define TIKU_NVM_RESERVED_BYTES  (256u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_RESERVED_BYTES  (128u * 1024u)   /* durable named-data tail */
#else
#define TIKU_NVM_RESERVED_BYTES  0u
#endif

/*
 * Filesystem (TFS) extent: a fixed slice of the region the /data file store
 * owns, sitting between the tier extent (front) and the reserved tail. Balanced
 * split. 0 where the file store rides its own backing (msp430 FRAM / host).
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVMFS_FS_BYTES  (1536u * 1024u)   /* 1.5 MB */
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVMFS_FS_BYTES  (512u * 1024u)    /* 512 KB (apollo4l) */
#elif defined(PLATFORM_RP2350)
#define TIKU_NVMFS_FS_BYTES  (2816u * 1024u)   /* 2.75 MB (rp2350 Flash FS) */
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVMFS_FS_BYTES  (16u * 1024u)     /* 16 KB: back half of the carved
                                                * RRAM region; the front stays a
                                                * raw-probe (nvmprobe) scratch */
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
