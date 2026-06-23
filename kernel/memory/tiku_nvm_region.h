/*
 * Tiku Operating System
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

/**
 * @brief Return the board's carved NVM region backend, or NULL if none.
 *
 * The returned backend is owned by the region layer (do not free).  Reads use
 * be->base directly; writes go through be->write inside an NVM unlock window.
 *
 * @return Pointer to the region backend, or NULL on parts without one.
 */
const tiku_nvm_backend_t *tiku_nvm_region_get(void);

#endif /* TIKU_NVM_REGION_H_ */
