/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.c - System VFS tree root assembly
 *
 * Builds the production VFS root from the per-subtree modules in
 * tree/ and calls tiku_vfs_init().  The actual node handlers live
 * in the modules; this file only orchestrates init and attaches
 * the top-level directories.
 *
 * Assembly model: the inner tree (every node below /sys and /dev)
 * is const data wired together at compile time inside the modules.
 * Only this top level is assembled at runtime, because /proc/
 * builds its node arrays dynamically and /data exists only in
 * BASIC-enabled builds — so the root's child list cannot be a
 * compile-time constant.  Each top-level module therefore exports
 * a _get() function returning its fully-formed directory node,
 * which is copied by value into the mutable root_children array
 * below (the same contract tiku_proc_vfs_get() has always had).
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree.h"
#include "tiku_vfs.h"
#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_proc_vfs.h>
#include "tree/tiku_vfs_tree_sys.h"
#include "tree/tiku_vfs_tree_boot.h"
#include "tree/tiku_vfs_tree_dev.h"
#include "tree/tiku_vfs_tree_data.h"

/*---------------------------------------------------------------------------*/
/* VFS TREE                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * /
 * ├── sys/   — tree/tiku_vfs_tree_sys.c (assembles boot, timer,
 * │            clock, watchdog, htimer, power, sched, init from
 * │            their own modules)
 * ├── dev/   — tree/tiku_vfs_tree_dev.c (assembles gpio, gpio_dir
 * │            from tree/tiku_vfs_tree_gpio.c)
 * ├── proc/  — kernel/process/tiku_proc_vfs.c
 * └── data/  — tree/tiku_vfs_tree_data.c (BASIC builds only)
 */

/**
 * Mutable root children: sys + dev + proc (+ optionally data).
 *
 * Sized for the maximum possible set; `vfs_root.child_count`
 * records the actual count populated at init time.  Placed in
 * .persistent (FRAM) to conserve SRAM for the stack — writes
 * happen only inside the init-time MPU unlock window below.
 */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    root_children[4];

/** Mutable root node (FRAM, written at init): name "" so that
 *  resolving "/" yields it directly. */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    vfs_root;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build and register the system VFS tree.
 *
 * Boot-time orchestration in three steps:
 *
 *   1. Module init, in dependency order: boot first — it must
 *      latch SYSRSTIV before anything else touches it.  dev brings
 *      up LED hardware; sys validates the RTC epoch and the
 *      device-name cell.  All persistent state is declared as
 *      magic-gated persist cells (TIKU_PERSIST_CELL); each module
 *      validates its own cells and the cell API owns the MPU
 *      unlock windows.
 *
 *   2. Root assembly: copy each top-level directory node (from
 *      the module _get() functions) into the FRAM-resident
 *      root_children array, inside one MPU unlock window.  /data
 *      is attached only when the BASIC interpreter is compiled in,
 *      and n_root records how many slots are live.
 *
 *   3. Hand the finished root to tiku_vfs_init(), after which
 *      every path is resolvable.
 *
 * Call once during boot, after hardware and process init (see
 * main.c); the drivers registry and shell start afterwards and
 * expect the tree to be live.
 */
void
tiku_vfs_tree_init(void)
{
    uint8_t n_root;
    uint16_t mpu_saved;

    /* Per-subtree init: boot first (captures SYSRSTIV before
     * anything else can clear it), then the modules with
     * hardware/persistent state.  Each module validates its own
     * persist cells — there is no cross-module first-boot flag. */
    tiku_vfs_tree_boot_init();
    tiku_vfs_tree_dev_init();
    tiku_vfs_tree_sys_init();

    /* Unlock FRAM — root_children, vfs_root and the /proc/ arrays
     * are all in .persistent (FRAM) to conserve SRAM for the
     * stack. */
    mpu_saved = tiku_mpu_unlock_nvm();

    root_children[0] = *tiku_vfs_tree_sys_get();
    root_children[1] = *tiku_vfs_tree_dev_get();

    /* Build and attach /proc/ (also writes to FRAM arrays) */
    root_children[2] = *tiku_proc_vfs_get();
    n_root = 3;

#if defined(TIKU_SHELL_ENABLE)
    /* /data: the dynamic file store (plus /data/basic when BASIC is built). */
    root_children[3] = *tiku_vfs_tree_data_get();
    n_root = 4;
#endif

    vfs_root = (tiku_vfs_node_t){
        "", TIKU_VFS_DIR, NULL, NULL, root_children, n_root
    };

    tiku_mpu_lock_nvm(mpu_saved);

    tiku_vfs_init(&vfs_root);
}
