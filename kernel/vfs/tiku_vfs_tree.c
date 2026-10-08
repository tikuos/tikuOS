/*
 * TikuOS system VFS tree assembly.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_tree.h"
#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_proc_vfs.h>
#include "tree/tiku_vfs_tree_sys.h"
#include "tree/tiku_vfs_tree_boot.h"
#include "tree/tiku_vfs_tree_dev.h"
#include "tree/tiku_vfs_tree_data.h"
#include <string.h>

static TIKU_DURABLE tiku_vfs_node_t root_children[4];
static TIKU_DURABLE tiku_vfs_node_t vfs_root;
static uint8_t subtrees_initialized;

/** @brief Validate optional nodes before initializing or publishing the tree. */
static int config_valid(const tiku_vfs_tree_config_t *config)
{
    unsigned i;
    unsigned j;

    if (config == NULL) {
        return TIKU_VFS_OK;
    }
    if (config->node_count > TIKU_VFS_BOOT_NODES_MAX ||
        config->node_count > TIKU_VFS_MOUNT_MAX ||
        config->data_child_count > TIKU_VFS_DATA_CHILDREN_MAX) {
        return TIKU_VFS_E2BIG;
    }
    if ((config->node_count && config->nodes == NULL) ||
        (config->data_child_count && config->data_children == NULL)) {
        return TIKU_VFS_EINVAL;
    }
    for (i = 0; i < config->data_child_count; i++) {
        if (tiku_vfs_validate_node(&config->data_children[i], 5u) != 0) {
            return TIKU_VFS_EINVAL;
        }
    }
    for (i = 0; i < config->node_count; i++) {
        const tiku_vfs_boot_node_t *entry = &config->nodes[i];
        const tiku_vfs_node_t *parent;
        int at_root;

        if (entry->parent == NULL) {
            return TIKU_VFS_EINVAL;
        }
        at_root = strcmp(entry->parent, "/") == 0;
        if (!at_root && strcmp(entry->parent, "/sys") != 0) {
            return TIKU_VFS_EINVAL;
        }
        if (tiku_vfs_validate_node(entry->node, at_root ? 0u : 4u) != 0) {
            return TIKU_VFS_EINVAL;
        }
        if (at_root) {
            const char *name = entry->node->name;
            if (!strcmp(name, "sys") || !strcmp(name, "dev") ||
                !strcmp(name, "proc") || !strcmp(name, "data")) {
                return TIKU_VFS_ECONFLICT;
            }
        } else {
            parent = tiku_vfs_tree_sys_get();
            for (j = 0; j < parent->child_count; j++) {
                if (!strcmp(entry->node->name, parent->children[j].name)) {
                    return TIKU_VFS_ECONFLICT;
                }
            }
        }
        for (j = 0; j < i; j++) {
            const tiku_vfs_boot_node_t *prev = &config->nodes[j];
            if (entry->node == prev->node ||
                (!strcmp(entry->parent, prev->parent) &&
                 !strcmp(entry->node->name, prev->node->name))) {
                return TIKU_VFS_ECONFLICT;
            }
        }
    }
    return TIKU_VFS_OK;
}

int tiku_vfs_tree_init(const tiku_vfs_tree_config_t *config)
{
    unsigned i;
    uint16_t saved;
    int status;

    tiku_vfs_init(NULL);
    status = config_valid(config);
    if (status != TIKU_VFS_OK) {
        return status;
    }
    if (!subtrees_initialized) {
        tiku_vfs_tree_boot_init();
        tiku_vfs_tree_dev_init();
        tiku_vfs_tree_sys_init();
        subtrees_initialized = 1;
    }
    saved = tiku_mpu_unlock_nvm();
    root_children[0] = *tiku_vfs_tree_sys_get();
    root_children[1] = *tiku_vfs_tree_dev_get();
    root_children[2] = *tiku_proc_vfs_get();
    root_children[3] = *tiku_vfs_tree_data_get();
    if (config != NULL) {
        root_children[3].children = config->data_children;
        root_children[3].child_count = config->data_child_count;
    }
    vfs_root = (tiku_vfs_node_t){
        .name = "", .type = TIKU_VFS_DIR,
        .children = root_children, .child_count = 4
    };
    if (tiku_mpu_lock_nvm_status(saved) != TIKU_MEM_OK) {
        return TIKU_VFS_EIO;
    }
    tiku_vfs_init(&vfs_root);
    for (i = 0; config != NULL && i < config->node_count; i++) {
        status = tiku_vfs_mount(config->nodes[i].parent,
                                config->nodes[i].node);
        if (status != TIKU_VFS_OK) {
            tiku_vfs_init(NULL);
            return status;
        }
    }
    return TIKU_VFS_OK;
}
