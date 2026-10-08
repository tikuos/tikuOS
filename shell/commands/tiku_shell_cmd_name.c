/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_name.c - "name" command implementation.
 *
 * Reads or sets /sys/device/name.  The underlying node is held in durable
 * memory, so a change persists across reset and power loss.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_name.h"
#include <shell/tiku_shell.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>

#define DEVICE_NAME_PATH "/sys/device/name"

void
tiku_shell_cmd_name(uint8_t argc, const char *argv[])
{
    char buf[40];
    int n;

    if (argc < 2) {
        n = tiku_vfs_read(DEVICE_NAME_PATH, buf, sizeof(buf) - 1);
        if (n < 0) {
            SHELL_PRINTF("name: read failed\n");
            return;
        }
        if (n > (int)sizeof(buf) - 1) {     /* keep the NUL inside the buffer */
            n = (int)sizeof(buf) - 1;
        }
        buf[n] = '\0';
        SHELL_PRINTF("%s", buf);
        return;
    }

    if (tiku_vfs_write(DEVICE_NAME_PATH, argv[1],
                       strlen(argv[1])) < 0) {
        SHELL_PRINTF("name: invalid value\n");
        return;
    }

    n = tiku_vfs_read(DEVICE_NAME_PATH, buf, sizeof(buf));
    if (n < 0) {
        SHELL_PRINTF("name: written, but readback failed\n");
        return;
    }
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    buf[n] = '\0';
    SHELL_PRINTF("name set to '%s'\n", buf);
}
