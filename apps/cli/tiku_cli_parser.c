/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_parser.c - Command-line text parser implementation
 *
 * Splits a line into tokens, matches the first token against the
 * registered command table, and invokes the handler.
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

#include "tiku_cli_parser.h"
#include "tiku_cli_io.h"       /* CLI_PRINTF */

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_cli_cmd_t *cmd_table = (void *)0;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings.
 * @return 0 if equal, non-zero otherwise.
 */
static int
cli_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_cli_parser_init(const tiku_cli_cmd_t *commands)
{
    cmd_table = commands;
}

void
tiku_cli_parser_execute(char *line)
{
    const char *argv[TIKU_CLI_MAX_ARGS];
    uint8_t argc = 0;
    char *p = line;
    const tiku_cli_cmd_t *cmd;

    if (!cmd_table) {
        return;
    }

    /* ---- Tokenize by spaces ---- */
    while (*p && argc < TIKU_CLI_MAX_ARGS) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }

    if (argc == 0) {
        return;
    }

    /* ---- Dispatch ---- */
    for (cmd = cmd_table; cmd->name != NULL; cmd++) {
        if (cli_strcmp(argv[0], cmd->name) == 0) {
            cmd->handler(argc, argv);
            return;
        }
    }

    CLI_PRINTF("Unknown command: %s\n", argv[0]);
    CLI_PRINTF("Type 'help' for a list of commands.\n");
}
