/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli.c - CLI process and command table
 *
 * Defines the command table, the "help" built-in, and the TikuOS
 * protothread that performs line editing and feeds completed lines
 * to the parser.  All I/O goes through the tiku_cli_io abstraction
 * so the same code works over UART, a network link, or an LLM pipe.
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

#include "tiku_cli.h"
#include "tiku_cli_config.h"
#include "tiku_cli_parser.h"
#include <kernel/timers/tiku_timer.h>

#if TIKU_CLI_TCP_ENABLE
#include "tiku_cli_io_tcp.h"
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>  /* tiku_kits_net_process */
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND HEADERS                                                           */
/*---------------------------------------------------------------------------*/

#if TIKU_CLI_CMD_PS
#include "commands/tiku_cli_cmd_ps.h"
#endif
#if TIKU_CLI_CMD_INFO
#include "commands/tiku_cli_cmd_info.h"
#endif
#if TIKU_CLI_CMD_TIMER
#include "commands/tiku_cli_cmd_timer.h"
#endif
#if TIKU_CLI_CMD_KILL
#include "commands/tiku_cli_cmd_kill.h"
#endif
#if TIKU_CLI_CMD_RESUME
#include "commands/tiku_cli_cmd_resume.h"
#endif

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

#if TIKU_CLI_CMD_HELP
static void tiku_cli_cmd_help(uint8_t argc, const char *argv[]);
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND TABLE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Static command table (NULL-terminated sentinel)
 *
 * To add a new command:
 *   1. Create the handler in apps/cli/commands/tiku_cli_cmd_xxx.c
 *   2. Add a TIKU_CLI_CMD_XXX flag to tiku_cli_config.h
 *   3. #include the header above and add an entry here
 *   4. Add the .c to the Makefile (APP=cli section)
 */
static const tiku_cli_cmd_t tiku_cli_commands[] = {
#if TIKU_CLI_CMD_HELP
    {"help", "Show available commands",  tiku_cli_cmd_help},
#endif
#if TIKU_CLI_CMD_PS
    {"ps",   "List active processes",    tiku_cli_cmd_ps},
#endif
#if TIKU_CLI_CMD_INFO
    {"info", "System overview",          tiku_cli_cmd_info},
#endif
#if TIKU_CLI_CMD_TIMER
    {"timer", "Software timer status",   tiku_cli_cmd_timer},
#endif
#if TIKU_CLI_CMD_KILL
    {"kill",   "Stop a process",          tiku_cli_cmd_kill},
#endif
#if TIKU_CLI_CMD_RESUME
    {"resume", "Resume a stopped process", tiku_cli_cmd_resume},
#endif
    {NULL, NULL, NULL}
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_cli_cmd_t *
tiku_cli_get_commands(void)
{
    return tiku_cli_commands;
}

/*---------------------------------------------------------------------------*/
/* BUILT-IN COMMANDS                                                         */
/*---------------------------------------------------------------------------*/

#if TIKU_CLI_CMD_HELP
/**
 * @brief "help" — print every registered command and its description.
 */
static void
tiku_cli_cmd_help(uint8_t argc, const char *argv[])
{
    const tiku_cli_cmd_t *cmd;

    (void)argc;
    (void)argv;

    CLI_PRINTF("Available commands:\n");
    for (cmd = tiku_cli_commands; cmd->name != NULL; cmd++) {
        CLI_PRINTF("  %s - %s\n", cmd->name, cmd->help);
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* CLI PROCESS                                                               */
/*---------------------------------------------------------------------------*/

/** Line buffer and cursor (static — no dynamic allocation) */
static char line_buf[TIKU_CLI_LINE_SIZE];
static uint8_t line_pos;

/** Periodic timer for I/O polling */
static struct tiku_timer cli_timer;

/** The CLI process */
TIKU_PROCESS(tiku_cli_process, "CLI");

TIKU_PROCESS_THREAD(tiku_cli_process, ev, data)
{
    int ch;

    (void)data;

    TIKU_PROCESS_BEGIN();

    /* ---- One-time init ---- */
    tiku_cli_parser_init(tiku_cli_commands);
    line_pos = 0;

#if TIKU_CLI_TCP_ENABLE
    tiku_cli_io_tcp_init();
    /* Banner deferred until a TCP client connects (see loop below) */
#else
    tiku_cli_io_set_backend(&tiku_cli_io_uart);
    CLI_PRINTF("\n--- TikuOS CLI ---\n");
    CLI_PRINTF("Type 'help' for available commands.\n");
    CLI_PRINTF("tikuOS> ");
#endif

    tiku_timer_set_event(&cli_timer, TIKU_CLI_POLL_TICKS);

    /* ---- Main loop ---- */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

#if TIKU_CLI_TCP_ENABLE
        /* --- TCP connection lifecycle --- */
        if (!tiku_cli_io_tcp_is_connected()) {
            /* Not connected — if we were, clear the backend */
            if (tiku_cli_io_get_backend() == &tiku_cli_io_tcp) {
                tiku_cli_io_set_backend((void *)0);
                line_pos = 0;
            }
            tiku_timer_reset(&cli_timer);
            continue;
        }
        /* New connection arrived — install backend and show banner */
        if (tiku_cli_io_get_backend() != &tiku_cli_io_tcp) {
            tiku_cli_io_set_backend(&tiku_cli_io_tcp);
            line_pos = 0;
            CLI_PRINTF("\n--- TikuOS Telnet Shell ---\n");
            CLI_PRINTF("Type 'help' for available commands.\n");
            CLI_PRINTF("tikuOS> ");
            tiku_cli_io_tcp_flush();
        }
#endif

        /* Drain all available characters from the backend */
        while (tiku_cli_io_rx_ready()) {
            ch = tiku_cli_io_getc();
            if (ch < 0) {
                break;
            }

            if (ch == '\r' || ch == '\n') {
                /* End of line — parse and dispatch */
                CLI_PRINTF("\n");
                line_buf[line_pos] = '\0';
                if (line_pos > 0) {
                    tiku_cli_parser_execute(line_buf);
                }
                line_pos = 0;
                CLI_PRINTF("tikuOS> ");

            } else if (ch == '\b' || ch == 127) {
                /* Backspace */
                if (line_pos > 0) {
                    line_pos--;
                    if (tiku_cli_io_has_echo()) {
                        CLI_PRINTF("\b \b");
                    }
                }

            } else if (line_pos < TIKU_CLI_LINE_SIZE - 1) {
                /* Printable character — store and optionally echo */
                line_buf[line_pos++] = (char)ch;
                if (tiku_cli_io_has_echo()) {
                    tiku_cli_io_putc((char)ch);
                }
            }
        }

#if TIKU_CLI_TCP_ENABLE
        tiku_cli_io_tcp_flush();
#endif
        tiku_timer_reset(&cli_timer);
    }

    TIKU_PROCESS_END();
}

/* Auto-start the CLI process (and the net process when TCP is enabled) */
#if TIKU_CLI_TCP_ENABLE
TIKU_AUTOSTART_PROCESSES(&tiku_kits_net_process, &tiku_cli_process);
#else
TIKU_AUTOSTART_PROCESSES(&tiku_cli_process);
#endif
