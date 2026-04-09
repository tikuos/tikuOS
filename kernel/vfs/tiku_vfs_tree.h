/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.h - System VFS tree (production, not test)
 *
 * Builds and initialises the root VFS tree with /sys, /dev, /proc.
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
 * Sets up /sys (uptime, mem), /dev (led0, led1), /proc.
 * Call once during boot, after hardware and process init.
 */
void tiku_vfs_tree_init(void);

/**
 * @brief Set the boot count value exposed via /sys/boot/count.
 *
 * Call from the hibernate resume path after reading the marker.
 */
void tiku_vfs_set_boot_count(uint32_t count);

#endif /* TIKU_VFS_TREE_H_ */
