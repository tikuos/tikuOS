/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.c - system VFS tree root assembly.
 *
 * Builds the production root from the per-subtree modules in tree/.  The inner
 * tree is const data wired at compile time; only this top level is assembled at
 * run time, because /proc builds its arrays dynamically and /data is optional.
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
static TIKU_DURABLE tiku_vfs_node_t root_children[4];

/** Mutable root node (FRAM, written at init): name "" so that
 *  resolving "/" yields it directly. */
static TIKU_DURABLE tiku_vfs_node_t vfs_root;

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

/* Value, not defined(): tiku.h defines TIKU_SHELL_ENABLE unconditionally (0
 * when the shell is off), so `defined()` would always be true and this would
 * reference a node that is not compiled. */
#if TIKU_SHELL_ENABLE
    /* /data: the dynamic file store (plus /data/basic when BASIC is built).
     * The STORE itself is always built -- kernel-level tenants reach it through
     * tiku_vfs_tree_data_store() -- but presenting it as a namespace entry is a
     * shell concern. */
    root_children[3] = *tiku_vfs_tree_data_get();
    n_root = 4;
#endif

    vfs_root = (tiku_vfs_node_t){
        "", TIKU_VFS_DIR, NULL, NULL, root_children, n_root
    };

    tiku_mpu_lock_nvm(mpu_saved);

    tiku_vfs_init(&vfs_root);
}
