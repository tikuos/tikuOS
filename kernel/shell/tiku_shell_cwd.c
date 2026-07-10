/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cwd.c - Shell current working directory and path resolution
 *
 * Maintains the shell's current working directory as a single static
 * SRAM string (always absolute, always starting with '/', at most
 * TIKU_SHELL_CWD_SIZE bytes including the NUL) and provides path
 * resolution so shell commands can accept both absolute and relative
 * paths.  The cwd is process-global to the shell and is not persistent
 * -- it resets to "/" on every reboot.
 *
 * tiku_shell_cwd_resolve() collapses a user-supplied path into a clean
 * absolute path: an input starting with '/' is resolved from root,
 * otherwise from the current cwd; "." components are dropped, ".."
 * pops one component (clamping at root), and runs of '/' are
 * coalesced.  Resolution is purely lexical string manipulation -- it
 * does NOT consult the VFS, so it neither verifies the path exists nor
 * follows any links.  Callers that need an existing target (e.g. the
 * `cd`, `ls`, `read` commands) resolve first and then validate the
 * result with tiku_vfs_resolve(); the absolute form produced here is
 * exactly the input that tiku_vfs_resolve() requires.
 *
 * All three helpers operate in place on a caller-provided buffer and
 * are bounded by the buffer size passed in: when a component would not
 * fit, append_component() truncates silently rather than overflowing.
 * No dynamic allocation is used anywhere in this module.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cwd.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * The shell's current working directory (SRAM, not persistent).
 *
 * Always an absolute path beginning with '/', held without a trailing
 * slash except for the root "/" itself.  Initialised to root and reset
 * to root on every boot.  Read via tiku_shell_cwd_get(); replaced via
 * tiku_shell_cwd_set(); used as the base for relative resolution in
 * tiku_shell_cwd_resolve().  Capacity is TIKU_SHELL_CWD_SIZE bytes
 * including the NUL (see tiku_shell_cwd.h).
 */
static char cwd[TIKU_SHELL_CWD_SIZE] = "/";

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Strip trailing '/' characters from a path, in place.
 *
 * Removes every trailing slash but never shortens the path below one
 * character, so the root "/" is preserved.  Used to normalise both the
 * stored cwd and freshly resolved paths to the no-trailing-slash form.
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
 * Drops any trailing slash, then deletes back to and including the
 * last '/', leaving the parent directory.  The result is clamped to
 * root: applying ".." at or above "/" yields "/" rather than an empty
 * string, so a path can never walk above the filesystem root.
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
 * Adds a '/' before the component unless @p path is still at root
 * (length 1), then copies up to @p complen characters of @p comp and
 * NUL-terminates.  Strictly bounded by @p pathsz: if the separator or
 * the component would not fit, the excess is dropped silently (the
 * function neither reports nor signals truncation -- the path is just
 * shorter than the source implied).
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
 * Returns a pointer to the internal cwd buffer; it is always an
 * absolute, NUL-terminated path beginning with '/'.  The caller must
 * not modify or free it, and the contents may change on the next
 * tiku_shell_cwd_set().
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
 * Stores @p path as the new cwd, truncating to TIKU_SHELL_CWD_SIZE - 1
 * characters and stripping any trailing slash.  The path must already
 * be absolute: a NULL pointer or one not starting with '/' is ignored
 * (the cwd is left unchanged).  This routine does NOT check that the
 * directory exists -- existence/type validation is the caller's job
 * (e.g. `cd` validates with tiku_vfs_resolve() before calling this).
 *
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
 * Builds the result in @p out from a base that depends on @p input: if
 * @p input starts with '/', the base is root; otherwise it is the
 * current cwd.  The remaining path is then walked component by
 * component -- runs of '/' are skipped, "." is ignored, ".." pops one
 * component (clamped at root via go_up()), and anything else is
 * appended via append_component().  A trailing slash is stripped and an
 * empty result is forced back to "/", so @p out is always a non-empty
 * absolute path on return.
 *
 * Resolution is purely lexical: the VFS is never consulted, so this
 * neither verifies that the path exists nor resolves links.  All
 * writes are bounded by @p outsz; an over-long path is truncated
 * silently (see append_component()).  @p out is left untouched only
 * when @p outsz is 0.
 *
 * @param input  User-supplied path, absolute or relative to the cwd
 * @param out    Output buffer receiving the resolved absolute path
 * @param outsz  Capacity of @p out in bytes, including the NUL
 */
void
tiku_shell_cwd_resolve(const char *input, char *out, uint8_t outsz)
{
    const char *p;
    const char *comp;
    uint8_t complen;

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

    /* Walk through each component */
    while (*p != '\0') {
        /* Skip slashes */
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* Extract component */
        comp = p;
        while (*p != '/' && *p != '\0') {
            p++;
        }
        complen = (uint8_t)(p - comp);

        /* Handle ".." */
        if (complen == 2 && comp[0] == '.' && comp[1] == '.') {
            go_up(out);
        /* Handle "." (no-op) */
        } else if (complen == 1 && comp[0] == '.') {
            continue;
        } else {
            append_component(out, outsz, comp, complen);
        }
    }

    strip_trailing_slash(out);

    /* Ensure at least "/" */
    if (out[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
    }
}
