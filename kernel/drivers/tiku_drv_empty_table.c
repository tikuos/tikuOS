/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_empty_table.c - Zero-length driver table
 *
 * Linked when HAS_DRIVERS is NOT set (the drivers/ repo
 * is absent from the tree). Provides the symbols the kernel
 * registry expects so a clean Apache-2.0 core kernel build links
 * without requiring any external driver code.
 *
 * When drivers/ IS present, drivers/tiku_drv_table.c
 * provides non-empty definitions of the same symbols and this
 * file is excluded from the build by the Makefile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drv.h"

/* Length-zero array is a GNU extension; pick an idiom that builds
 * under -pedantic too. The single NULL element costs 4 bytes of
 * flash, which is below any meaningful budget. */
const tiku_drv_t *const tiku_drv_table[1] = { (const tiku_drv_t *)0 };
const uint8_t           tiku_drv_table_count = 0U;
