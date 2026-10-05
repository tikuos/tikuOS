/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.c - system VFS tree root assembly.
 *
 * Builds the root from the per-subtree modules in tree/.  The subtrees are
 * const data; this top level is assembled at run time because /proc builds its
 * arrays at run time.  Drivers mount their nodes after it.
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
#if TIKU_APPL_GUI
#include "tiku_gui.h"           /* applications overlay: /gui */
#include "tiku_draw.h"          /* applications overlay: its console channel */
#define ROOT_SLOTS 5
#else
#define ROOT_SLOTS 4
#endif

/*---------------------------------------------------------------------------*/
/* VFS TREE                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * /
 * ├── sys/   — tree/tiku_vfs_tree_sys.c (assembles boot, timer,
 * │            clock, watchdog, htimer, power, net, sched, init and
 * │            other subtrees from their own modules)
 * ├── dev/   — tree/tiku_vfs_tree_dev.c (assembles gpio, gpio_dir and
 * │            gpio_owner from tree/tiku_vfs_tree_gpio.c and sensors
 * │            from tree/tiku_vfs_tree_sensor.c); drivers mount their
 * │            nodes under dev/<class>/
 * ├── proc/  — kernel/process/tiku_proc_vfs.c
 * ├── data/  — tree/tiku_vfs_tree_data.c, in every build
 * └── gui/   — the applications overlay, when TIKU_APPL_GUI is set
 */

/*
 * Mutable root children: sys + dev + proc + data (+ gui with TIKU_APPL_GUI).
 * Sized for the largest set; vfs_root.child_count records how many are
 * filled.  Durable memory, written only inside the MPU unlock window of
 * tiku_vfs_tree_init().
 */
static TIKU_DURABLE tiku_vfs_node_t root_children[ROOT_SLOTS];

/** Mutable root node, written at init.  Resolving "/" returns it without
 *  a name match, so its name is empty. */
static TIKU_DURABLE tiku_vfs_node_t vfs_root;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build and register the system VFS tree.
 *
 * Runs the module inits in dependency order -- boot first, because it latches
 * the reset cause before anything else can clear it -- copies each top-level
 * directory into the durable root, then calls tiku_vfs_init().
 *
 * @note Call once during boot, after hardware and process init; the drivers
 *       registry and the shell start afterwards and expect a live tree.
 */
void
tiku_vfs_tree_init(void)
{
    uint8_t n_root;
    uint16_t mpu_saved;

    /* Per-subtree init: boot first (captures the reset cause before
     * anything else can clear it), then the modules with
     * hardware/persistent state.  Each module validates its own
     * persist cells. */
    tiku_vfs_tree_boot_init();
    tiku_vfs_tree_dev_init();
    tiku_vfs_tree_sys_init();

    /* Unlock NVM: root_children and vfs_root are TIKU_DURABLE, and the
     * /proc arrays written below are TIKU_RETAINED. */
    mpu_saved = tiku_mpu_unlock_nvm();

    root_children[0] = *tiku_vfs_tree_sys_get();
    root_children[1] = *tiku_vfs_tree_dev_get();

    /* Build and attach /proc/ (also writes its retained arrays) */
    root_children[2] = *tiku_proc_vfs_get();
    n_root = 3;

    /* /data: the dynamic file store (plus /data/basic when BASIC is built),
     * attached with or without the shell. */
    root_children[3] = *tiku_vfs_tree_data_get();
    n_root = 4;
#if TIKU_APPL_GUI
    /* /gui: the forms a process publishes for a host desktop to draw, and
     * the console channel that desktop's window session rides. */
    root_children[n_root++] = *tiku_gui_vfs_get();
    tiku_draw_init();
#endif

    vfs_root = (tiku_vfs_node_t){
        "", TIKU_VFS_DIR, NULL, NULL, root_children, n_root
    };

    tiku_mpu_lock_nvm(mpu_saved);

    tiku_vfs_init(&vfs_root);
}
