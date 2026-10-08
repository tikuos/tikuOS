/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku.h"
#include "tiku_boot_vfs.h"
#include <kernel/vfs/tiku_vfs_tree.h>
#include <kernel/drivers/tiku_drv_registry.h>
#if TIKU_SHELL_ENABLE
#include <shell/tiku_shell_vfs.h>
#endif
#if TIKU_INIT_ENABLE
#include <services/init/tiku_init_vfs.h>
#endif
#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC
#include <basic/tiku_basic_vfs.h>
#endif
#if TIKU_APPL_GUI
#include "tiku_gui.h"
#include "tiku_draw.h"
#endif

/** @brief Select providers without loading files or executing shell commands. */
static int assemble(void)
{
    tiku_vfs_boot_node_t nodes[TIKU_VFS_BOOT_NODES_MAX];
    tiku_vfs_tree_config_t config = {0};

    config.nodes = nodes;
#if TIKU_SHELL_ENABLE
    nodes[config.node_count++] = (tiku_vfs_boot_node_t){
        "/sys", tiku_shell_vfs_rules()
    };
    nodes[config.node_count++] = (tiku_vfs_boot_node_t){
        "/sys", tiku_shell_vfs_jobs()
    };
#endif
#if TIKU_INIT_ENABLE
    nodes[config.node_count++] = (tiku_vfs_boot_node_t){
        "/sys", tiku_init_vfs_get()
    };
#endif
#if TIKU_SHELL_ENABLE && TIKU_SHELL_CMD_BASIC
    config.data_children = tiku_basic_vfs_get();
    config.data_child_count = 1;
#endif
#if TIKU_APPL_GUI
    nodes[config.node_count++] = (tiku_vfs_boot_node_t){
        "/", tiku_gui_vfs_get()
    };
#endif
    return tiku_vfs_tree_init(&config);
}

int tiku_boot_vfs_init(void)
{
    int status = assemble();

#if TIKU_APPL_GUI
    if (status == 0) {
        tiku_draw_init();
    }
#endif
    return status;
}

int tiku_boot_vfs_restore(void)
{
    int status = assemble();

    if (status == 0) {
        tiku_drv_remount_all();
    }
    return status;
}
