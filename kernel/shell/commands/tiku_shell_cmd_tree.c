/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_tree.c - "tree" command implementation
 *
 * Walks a VFS subtree depth-first and prints each entry indented
 * by its depth.  Implementation is a small recursive function
 * that takes the current node and the remaining depth budget;
 * the budget caps recursion so a malformed tree (or one that
 * grows beyond expectation on a future device) cannot exhaust
 * the stack on a 2 KB-SRAM target.
 *
 * The VFS exposes `children` and `child_count` directly on the
 * `tiku_vfs_node_t` struct, so we recurse on the in-memory tree
 * rather than re-resolving paths.  This avoids carrying a path
 * buffer down the stack and keeps each frame small (one pointer,
 * one int, one short loop counter).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_tree.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>

/* Two indent units per directory level keeps deep paths readable
 * within an 80-column terminal.  At the cap of 8 levels the leaf
 * line still fits comfortably. */
#ifndef TIKU_SHELL_TREE_MAX_DEPTH
#define TIKU_SHELL_TREE_MAX_DEPTH 8
#endif

/**
 * @brief Print the leading indentation guides for a tree level.
 */
static void
tree_print_indent(uint8_t depth, uint8_t is_last_chain)
{
    uint8_t i;
    /* Leading vertical guides for ancestor levels.  Without per-level
     * "last child" tracking we cannot draw the full guide-set
     * faithfully; using a uniform "|  " for ancestors and the proper
     * connector for the current level keeps the output unambiguous
     * while costing one byte of stack instead of one bit per level. */
    (void)is_last_chain;
    for (i = 0; i < depth; i++) {
        SHELL_PRINTF("|  ");
    }
}

/**
 * @brief Recursively print a VFS directory subtree, one node per line.
 *
 * Draws "|-- " / "`-- " connectors for each child, recursing into
 * subdirectories until TIKU_SHELL_TREE_MAX_DEPTH, where it prints "...".
 *
 * @param node   Directory node to walk (non-directories produce nothing).
 * @param depth  Current nesting level, used for indentation.
 */
static void
tree_walk(const tiku_vfs_node_t *node, uint8_t depth)
{
    uint8_t i;

    if (node->type != TIKU_VFS_DIR ||
        node->children == (const tiku_vfs_node_t *)0 ||
        node->child_count == 0) {
        return;
    }

    for (i = 0; i < node->child_count; i++) {
        const tiku_vfs_node_t *child = &node->children[i];
        const char *connector = (i + 1 == node->child_count) ? "`-- " : "|-- ";

        tree_print_indent(depth, 0);
        if (child->type == TIKU_VFS_DIR) {
            SHELL_PRINTF("%s%s/\n", connector, child->name);
            if (depth + 1 < TIKU_SHELL_TREE_MAX_DEPTH) {
                tree_walk(child, (uint8_t)(depth + 1));
            } else {
                tree_print_indent((uint8_t)(depth + 1), 0);
                SHELL_PRINTF("...\n");
            }
        } else {
            SHELL_PRINTF("%s%s\n", connector, child->name);
        }
    }
}

void
tiku_shell_cmd_tree(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    const tiku_vfs_node_t *root;

    if (argc >= 2) {
        tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    } else {
        tiku_shell_cwd_resolve(".", resolved, sizeof(resolved));
    }

    root = tiku_vfs_resolve(resolved);
    if (root == (const tiku_vfs_node_t *)0) {
        SHELL_PRINTF("tree: '%s' not found\n", resolved);
        return;
    }
    if (root->type != TIKU_VFS_DIR) {
        SHELL_PRINTF("tree: '%s' is not a directory\n", resolved);
        return;
    }

    SHELL_PRINTF("%s\n", resolved);
    tree_walk(root, 0);
}
