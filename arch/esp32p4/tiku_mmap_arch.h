/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mmap_arch.h - P4 read-only flash mapping with checked physical bounds.
 *
 * Once tiku_p4_mmap_init() succeeds, flash offset N reads at
 * TIKU_P4_FLASH_VIRTUAL + N; reads are bounded by the 16 MiB flash size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_MMAP_ARCH_H_
#define TIKU_P4_MMAP_ARCH_H_
#include <stdint.h>
#ifndef TIKU_P4_FLASH_VIRTUAL
#define TIKU_P4_FLASH_VIRTUAL 0x40000000u
#endif
/** @brief Map the fitted 16 MiB flash through SPI0 and the fixed L2 cache.
 * @note CPU0 SRAM execution, parked secondary cores and exclusive MSPI
 * ownership are required. Flash mutations must use the SRAM MSPI cache guard.
 */
int tiku_p4_mmap_init(void);
/** @brief Remove flash mappings from SRAM; callers must stop mapped users. */
int tiku_p4_mmap_shutdown(void);
/** @brief Copy a bounded flash mapping into caller-owned internal SRAM. */
int tiku_p4_mmap_read(uint32_t offset, void *data, uint32_t length);
/** @brief Return whether the flash mapping has completed initialization. */
int tiku_p4_mmap_ready(void);
#endif
