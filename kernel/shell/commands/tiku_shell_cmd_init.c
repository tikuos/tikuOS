/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_init.c - "init" command implementation
 *
 * Shell interface to the FRAM-backed init table.  Allows listing,
 * adding, removing, enabling/disabling, and re-running boot entries
 * without recompiling.
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

#include "tiku_shell_cmd_init.h"
#include <kernel/shell/tiku_shell.h>        /* SHELL_PRINTF */
#include <kernel/init/tiku_init.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/** Simple string compare (avoids pulling in full strcmp on small targets) */
static uint8_t
cmd_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

/** Parse a decimal uint8 from string, return 0 on success */
static uint8_t
cmd_parse_u8(const char *s, uint8_t *out)
{
    uint16_t val = 0;
    if (s[0] == '\0') {
        return 1;
    }
    while (*s) {
        if (*s < '0' || *s > '9') {
            return 1;
        }
        val = val * 10 + (*s - '0');
        if (val > 255) {
            return 1;
        }
        s++;
    }
    *out = (uint8_t)val;
    return 0;
}

/**
 * @brief Concatenate argv[first..argc-1] into buf with spaces.
 *
 * The init command syntax is: init add <seq> <name> <cmd tokens...>
 * We need to reassemble the command tokens back into a single string.
 */
static void
cmd_join_args(char *buf, uint8_t bufsz,
              uint8_t argc, const char *argv[], uint8_t first)
{
    uint8_t pos = 0;
    uint8_t i;

    memset(buf, 0, bufsz);

    for (i = first; i < argc && pos < bufsz - 1; i++) {
        const char *p = argv[i];
        if (i > first && pos < bufsz - 1) {
            buf[pos++] = ' ';
        }
        while (*p && pos < bufsz - 1) {
            buf[pos++] = *p++;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: list                                                          */
/*---------------------------------------------------------------------------*/

static void
cmd_init_list(void)
{
    uint8_t count = tiku_init_count();
    uint8_t i;

    if (count == 0) {
        SHELL_PRINTF("(no init entries)\n");
        return;
    }

    for (i = 0; i < count; i++) {
        const tiku_init_entry_t *e = tiku_init_get(i);
        if (e == (const tiku_init_entry_t *)0) {
            continue;
        }
        SHELL_PRINTF(" %02u  %-12s [%s]  %s\n",
                     e->seq,
                     e->name,
                     e->enabled ? "on " : "off",
                     e->cmd);
    }
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: add                                                           */
/*---------------------------------------------------------------------------*/

static void
cmd_init_add(uint8_t argc, const char *argv[])
{
    uint8_t seq;
    char cmd_buf[TIKU_INIT_CMD_SIZE];

    /* init add <seq> <name> <cmd...> → argc >= 5 */
    if (argc < 5) {
        SHELL_PRINTF("Usage: init add <seq> <name> <cmd...>\n");
        return;
    }

    if (cmd_parse_u8(argv[2], &seq)) {
        SHELL_PRINTF("Error: seq must be 0-99\n");
        return;
    }
    if (seq > 99) {
        SHELL_PRINTF("Error: seq must be 0-99\n");
        return;
    }

    /* Reassemble the command from remaining args */
    cmd_join_args(cmd_buf, sizeof(cmd_buf), argc, argv, 4);

    if (tiku_init_add(seq, argv[3], cmd_buf) < 0) {
        SHELL_PRINTF("Error: init table full\n");
        return;
    }

    SHELL_PRINTF("OK: '%s' at seq %02u\n", argv[3], seq);
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: rm                                                            */
/*---------------------------------------------------------------------------*/

static void
cmd_init_rm(uint8_t argc, const char *argv[])
{
    if (argc < 3) {
        SHELL_PRINTF("Usage: init rm <name>\n");
        return;
    }

    if (tiku_init_remove(argv[2]) < 0) {
        SHELL_PRINTF("Error: '%s' not found\n", argv[2]);
        return;
    }

    SHELL_PRINTF("OK: removed '%s'\n", argv[2]);
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: enable / disable                                              */
/*---------------------------------------------------------------------------*/

static void
cmd_init_set_enable(uint8_t argc, const char *argv[], uint8_t en)
{
    if (argc < 3) {
        SHELL_PRINTF("Usage: init %s <name>\n",
                     en ? "enable" : "disable");
        return;
    }

    if (tiku_init_enable(argv[2], en) < 0) {
        SHELL_PRINTF("Error: '%s' not found\n", argv[2]);
        return;
    }

    SHELL_PRINTF("OK: %s '%s'\n",
                 en ? "enabled" : "disabled", argv[2]);
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMAND: run                                                           */
/*---------------------------------------------------------------------------*/

static void
cmd_init_run(void)
{
    uint8_t n = tiku_init_run_all();
    SHELL_PRINTF("Executed %u init entries\n", n);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_init(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("Usage: init <list|add|rm|enable|disable|run>\n");
        return;
    }

    if (cmd_streq(argv[1], "list")) {
        cmd_init_list();
    } else if (cmd_streq(argv[1], "add")) {
        cmd_init_add(argc, argv);
    } else if (cmd_streq(argv[1], "rm")) {
        cmd_init_rm(argc, argv);
    } else if (cmd_streq(argv[1], "enable")) {
        cmd_init_set_enable(argc, argv, 1);
    } else if (cmd_streq(argv[1], "disable")) {
        cmd_init_set_enable(argc, argv, 0);
    } else if (cmd_streq(argv[1], "run")) {
        cmd_init_run();
    } else {
        SHELL_PRINTF("Unknown: init %s\n", argv[1]);
        SHELL_PRINTF("Subcommands: list add rm enable disable run\n");
    }
}
