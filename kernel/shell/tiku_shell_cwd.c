/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cwd.c - shell working directory and path resolution.
 *
 * Keeps one always-absolute cwd string, reset to "/" each boot.  Resolution is
 * purely lexical -- it collapses ".", ".." and repeated slashes but never consults
 * the VFS -- so callers needing an existing target must validate the result.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cwd.h"
#include <kernel/vfs/tiku_vfs.h>   /* tiku_vfs_next_segment (header-only) */
#include <string.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * The shell's current working directory (SRAM, not persistent).
 *
 * Always absolute and beginning with '/', held without a trailing slash except
 * for root itself, and reset to root on every boot.
 */
static char cwd[TIKU_SHELL_CWD_SIZE] = "/";

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Strip trailing '/' characters from a path, in place.
 *
 * Removes every trailing slash but never shortens the path below one
 * character, so the root "/" is preserved.  Normalises both the stored cwd
 * and freshly resolved paths to the no-trailing-slash form.
 *
 * @param path  NUL-terminated path, modified in place
 */
static void
strip_trailing_slash(char *path)
{
    uint8_t len = (uint8_t)strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

/**
 * @brief Remove the last path component in place (handle "..").
 *
 * Drops any trailing slash, then deletes back to and including the last '/'.
 * The result is clamped to root, so ".." at or above "/" yields "/" and a path
 * can never walk above the filesystem root.
 *
 * @param path  NUL-terminated path, modified in place
 */
static void
go_up(char *path)
{
    uint8_t len = (uint8_t)strlen(path);

    /* Strip trailing slash first */
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }

    /* Find the last '/' and truncate */
    while (len > 1 && path[len - 1] != '/') {
        len--;
    }

    /* Keep at least "/" */
    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        /* Remove the trailing slash unless it's root */
        path[len - 1] = '\0';
        if (path[0] == '\0') {
            path[0] = '/';
            path[1] = '\0';
        }
    }
}

/**
 * @brief Append one path component, inserting a '/' separator.
 *
 * Adds a '/' unless @p path is still at root, then copies up to @p complen
 * characters and NUL-terminates.  Strictly bounded by @p pathsz: anything that
 * would not fit is dropped silently, without signalling truncation.
 *
 * @param path     Destination path, modified in place
 * @param pathsz   Capacity of @p path in bytes, including the NUL
 * @param comp     Component characters to append (not NUL-terminated)
 * @param complen  Number of characters of @p comp to append
 */
static void
append_component(char *path, uint8_t pathsz,
                 const char *comp, uint8_t complen)
{
    uint8_t len = (uint8_t)strlen(path);

    /* Add separator if not at root */
    if (len > 1 && len < pathsz - 1) {
        path[len++] = '/';
        path[len] = '\0';
    }

    /* Append component */
    while (complen > 0 && len < pathsz - 1) {
        path[len++] = *comp++;
        complen--;
    }
    path[len] = '\0';
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the shell's current working directory.
 *
 * A pointer to the internal buffer, always an absolute NUL-terminated path.
 * The caller must not modify or free it, and the contents may change on the
 * next tiku_shell_cwd_set().
 *
 * @return Pointer to the cwd string (never NULL).
 */
const char *
tiku_shell_cwd_get(void)
{
    return cwd;
}

/**
 * @brief Replace the current working directory.
 *
 * Stores @p path as the new cwd, truncating to TIKU_SHELL_CWD_SIZE - 1 and
 * stripping any trailing slash.  A NULL pointer or a path not starting with
 * '/' is ignored.
 *
 * @note Existence and type validation belong to the caller -- `cd` resolves
 *       through the VFS before calling this.
 * @param path  Absolute path to adopt (must start with '/')
 */
void
tiku_shell_cwd_set(const char *path)
{
    if (path == (const char *)0 || path[0] != '/') {
        return;
    }
    strncpy(cwd, path, TIKU_SHELL_CWD_SIZE - 1);
    cwd[TIKU_SHELL_CWD_SIZE - 1] = '\0';
    strip_trailing_slash(cwd);
}

/**
 * @brief Resolve a user-supplied path to a clean absolute path.
 *
 * Builds the result from root when @p input is absolute and from the cwd
 * otherwise, then walks it component by component: runs of '/' are skipped,
 * "." is ignored, ".." pops one (clamped at root) and anything else appends.
 *
 * @note Purely lexical -- the VFS is never consulted, so this neither verifies
 *       existence nor resolves links.  Writes are bounded by @p outsz and an
 *       over-long path truncates silently; @p out is always a non-empty
 *       absolute path unless @p outsz is 0.
 * @param input  User-supplied path, absolute or relative to the cwd
 * @param out    Output buffer receiving the resolved absolute path
 * @param outsz  Capacity of @p out in bytes, including the NUL
 */
void
tiku_shell_cwd_resolve(const char *input, char *out, uint8_t outsz)
{
    const char *p;
    const char *comp;
    size_t      complen;

    if (outsz == 0) {
        return;
    }

    /* Absolute path — start from root */
    if (input[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        p = input + 1;
    } else {
        /* Relative — start from cwd */
        strncpy(out, cwd, outsz - 1);
        out[outsz - 1] = '\0';
        p = input;
    }

    /* Walk through each component (shared path lexer -- see
     * tiku_vfs_next_segment for the slash-run/trailing-slash rules) */
    while (tiku_vfs_next_segment(&p, &comp, &complen)) {
        /* Handle ".." */
        if (complen == 2 && comp[0] == '.' && comp[1] == '.') {
            go_up(out);
        /* Handle "." (no-op) */
        } else if (complen == 1 && comp[0] == '.') {
            continue;
        } else {
            append_component(out, outsz, comp, (uint8_t)complen);
        }
    }

    strip_trailing_slash(out);

    /* Ensure at least "/" */
    if (out[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
    }
}
