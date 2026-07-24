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
 * Region layout. The NVM memory tier bump-allocates from the FRONT of the
 * region; the top TIKU_NVM_RESERVED_BYTES is held back for durable NAMED data
 * that needs a STABLE location across boots -- the tier never hands this tail
 * out, so a named consumer can own a fixed offset in [size - reserved, size).
 * 0 where the tier owns the whole region (no carved tail).
 *
 * Current tail tenants (kernel/shell/basic): the BASIC saved-program slot at
 * the tail BASE and the BASIC execution-state checkpoint slot at the tail TOP
 * (PERSIST / RUN RESUME); a _Static_assert next to the slot layout in
 * tiku_basic_ckpt.inl checks both fit.  Apollo510's tail is larger because its
 * HUGE-tier program slot (1700 lines, ~258 KB) plus the checkpoint slot
 * outgrew the shared 256 KB default.
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_RESERVED_BYTES  (320u * 1024u)   /* HUGE program + ckpt slots */
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_RESERVED_BYTES  (256u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_RESERVED_BYTES  (128u * 1024u)   /* durable named-data tail */
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_RESERVED_BYTES  (256u * 1024u)   /* BIG 1024-line save + ckpt */
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_RESERVED_BYTES  (64u * 1024u)    /* L15: 256-line save + ckpt */
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
#define TIKU_NVMFS_FS_BYTES  (256u * 1024u)    /* 256 KB RRAM FS extent (the
                                                * old 16 KB back-half + probe-
                                                * scratch split is gone: the
                                                * front is the NVM tier now,
                                                * Ambiq-parity layout) */
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
