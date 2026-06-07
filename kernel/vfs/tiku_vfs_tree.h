/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.h - System VFS tree (production, not test)
 *
 * Builds and initialises the root VFS tree with /sys, /dev, /proc
 * and (in BASIC builds) /data.  The node handlers live in the
 * per-subtree modules under kernel/vfs/tree/; this header is the
 * only thing the rest of the system needs to include to bring the
 * whole tree up.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_H_
#define TIKU_VFS_TREE_H_

#include <stdint.h>

/**
 * @brief Build and register the system VFS tree.
 *
 * Runs the per-subtree module inits (boot counter, reset cause,
 * LEDs, RTC epoch, device name), assembles the top-level
 * directories into the FRAM-resident root, and registers it with
 * tiku_vfs_init().  Call once during boot, after hardware and
 * process init — see tiku_vfs_tree.c for the ordering rationale.
 */
void tiku_vfs_tree_init(void);

/**
 * @brief Set the boot count value exposed via /sys/boot_count.
 *
 * Call from the hibernate resume path after reading the marker:
 * overrides only the SRAM mirror that reads are served from, while
 * the FRAM cell keeps its true monotonic count.  Defined in
 * tree/tiku_vfs_tree_boot.c, which owns the counter.
 *
 * @param count  Value subsequent /sys/boot_count reads will report
 */
void tiku_vfs_set_boot_count(uint32_t count);

#endif /* TIKU_VFS_TREE_H_ */
