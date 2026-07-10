/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cwd.h - Shell current working directory and path resolution
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CWD_H_
#define TIKU_SHELL_CWD_H_

#include <stdint.h>

/** Maximum path length for the working directory */
#define TIKU_SHELL_CWD_SIZE  48

/**
 * @brief Return the current working directory.
 * @return NUL-terminated path string (always starts with '/')
 */
const char *tiku_shell_cwd_get(void);

/**
 * @brief Set the current working directory.
 * @param path  Absolute path (must start with '/')
 */
void tiku_shell_cwd_set(const char *path);

/**
 * @brief Resolve a user-supplied path against the cwd.
 *
 * - Absolute paths (starting with '/') are returned as-is.
 * - ".." moves up one component.
 * - Relative paths are joined to the cwd.
 *
 * @param input  User-supplied path (absolute or relative)
 * @param out    Output buffer for the resolved absolute path
 * @param outsz  Size of the output buffer
 */
void tiku_shell_cwd_resolve(const char *input, char *out, uint8_t outsz);

#endif /* TIKU_SHELL_CWD_H_ */
