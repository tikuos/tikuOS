/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cwd.c - Shell current working directory and path resolution
 *
 * Maintains a current working directory string and provides path
 * resolution (absolute, relative, ".." handling) so shell commands
 * can accept both absolute and relative paths.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

static char cwd[TIKU_SHELL_CWD_SIZE] = "/";

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/** Remove trailing slash (unless root) */
static void
strip_trailing_slash(char *path)
{
    uint8_t len = (uint8_t)strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
}

/** Go up one directory component in-place */
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

/** Append a component to path with '/' separator */
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

const char *
tiku_shell_cwd_get(void)
{
    return cwd;
}

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
