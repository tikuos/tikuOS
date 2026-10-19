/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
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

/** Size of the working-directory buffer, NUL included */
#define TIKU_SHELL_CWD_SIZE  48

/**
 * @brief Return the current working directory.
 * @return The shell's own buffer, never NULL: a NUL-terminated path that starts
 *         with '/' and has no trailing slash except at root.  Do not modify it;
 *         its contents change on the next tiku_shell_cwd_set().
 */
const char *tiku_shell_cwd_get(void);

/**
 * @brief Set the current working directory; NULL or a relative path is
 *        ignored, and a longer path is cut to TIKU_SHELL_CWD_SIZE - 1.
 *
 * Trailing slashes are stripped, except from root.
 *
 * @param path  Absolute path (must start with '/')
 * @note The path is not checked against the VFS; `cd` resolves it there
 *       before calling this.
 */
void tiku_shell_cwd_set(const char *path);

/**
 * @brief Resolve a user-supplied path against the cwd.
 *
 * Lexical only, so the result may name nothing: an absolute path starts from
 * '/', a relative one from the cwd; ".." moves up one component (clamped at
 * root), and ".", repeated slashes and trailing slashes are dropped.
 *
 * @param input  User-supplied path (absolute or relative)
 * @param out    Output buffer for the resolved absolute path
 * @param outsz  Size of the output buffer
 * @note A result longer than @p outsz - 1 is cut.  @p out is a non-empty
 *       absolute path when @p outsz is at least 2; a 1-byte @p out gets an
 *       empty string, and a 0-byte one is not written.
 */
void tiku_shell_cwd_resolve(const char *input, char *out, uint8_t outsz);

#endif /* TIKU_SHELL_CWD_H_ */
