/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_empty_table.c - zero-length driver table.
 *
 * Linked when HAS_DRIVERS is not set, so the core links without the drivers/
 * repo; when drivers/ is present, its tiku_drv_table.c defines these symbols
 * and the Makefile leaves this file out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drv.h"

/* A zero-length array is a GNU extension, so the table holds one NULL
 * element and the count says it is empty. */
const tiku_drv_t *const tiku_drv_table[1] = { (const tiku_drv_t *)0 };
const uint8_t           tiku_drv_table_count = 0U;
