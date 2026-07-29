/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_tree.h - "tree" command: recursive VFS listing
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TREE_H_
#define TIKU_SHELL_CMD_TREE_H_

#include <stdint.h>

/**
 * @brief "tree" command -- recursive depth-first VFS dump.
 *
 * Lists the current working directory, or the subtree rooted at a given path,
 * using ASCII connectors with directories marked by a trailing '/'.  Recursion
 * is capped by TIKU_SHELL_TREE_MAX_DEPTH so a looping VFS cannot blow the stack.
 */
void tiku_shell_cmd_tree(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TREE_H_ */
