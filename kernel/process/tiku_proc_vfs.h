/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proc_vfs.h - VFS /proc/ node registration
 *
 * Exposes process observability data through the VFS:
 *   /proc/count           → number of registered processes
 *   /proc/<pid>/name      → process name
 *   /proc/<pid>/state     → running / ready / waiting / sleeping / stopped
 *   /proc/<pid>/pid       → numeric pid
 *   /proc/<pid>/sram_used → SRAM bytes
 *   /proc/<pid>/fram_used → FRAM bytes
 *   /proc/<pid>/uptime    → seconds since start
 *   /proc/<pid>/wake_count→ times scheduled
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

#ifndef TIKU_PROC_VFS_H_
#define TIKU_PROC_VFS_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the /proc VFS directory node
 *
 * Returns a pointer to the top-level "proc" directory node that
 * can be included in the VFS root tree. The node tree is rebuilt
 * each time this function is called to reflect the current
 * process registry state.
 *
 * @return Pointer to the "proc" VFS directory node
 */
const tiku_vfs_node_t *tiku_proc_vfs_get(void);

/**
 * @brief Get the number of child nodes under /proc
 *
 * @return child_count of the /proc directory node
 */
uint8_t tiku_proc_vfs_child_count(void);

#endif /* TIKU_PROC_VFS_H_ */
