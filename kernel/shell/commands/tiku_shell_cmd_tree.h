/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_tree.h - "tree" command: recursive VFS listing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TREE_H_
#define TIKU_SHELL_CMD_TREE_H_

#include <stdint.h>

/**
 * @brief "tree" command -- recursive depth-first VFS dump.
 *
 * Usage:
 *   tree           -- list the current working directory
 *   tree <path>    -- list the subtree rooted at <path>
 *
 * Output uses ASCII tree connectors so the structure reads at a
 * glance (similar to the GNU `tree` utility); directories are
 * marked with a trailing '/'.  Recursion is bounded by a static
 * depth cap (TIKU_SHELL_TREE_MAX_DEPTH) so a malformed VFS that
 * loops cannot blow the small MSP430 stack.
 */
void tiku_shell_cmd_tree(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TREE_H_ */
