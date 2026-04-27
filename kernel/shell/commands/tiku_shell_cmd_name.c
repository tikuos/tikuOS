/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_name.c - "name" command implementation
 *
 * Reads or sets /sys/device/name. With no argument, prints the
 * current device name; with one argument, writes it. The
 * underlying VFS node is FRAM-backed so the change persists
 * across reset and power loss.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_name.h"
#include <kernel/shell/tiku_shell.h>
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
        buf[n] = '\0';
        SHELL_PRINTF("%s", buf);
        return;
    }

    if (tiku_vfs_write(DEVICE_NAME_PATH, argv[1],
                       (uint8_t)strlen(argv[1])) < 0) {
        SHELL_PRINTF("name: invalid value\n");
        return;
    }

    SHELL_PRINTF("name set to '%s'\n", argv[1]);
}
