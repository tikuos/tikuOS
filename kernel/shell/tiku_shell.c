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
 * to the parser.  All I/O goes through the tiku_shell_io abstraction
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

#include "tiku_shell.h"
#include "tiku_shell_config.h"
#include "tiku_shell_parser.h"
#include <kernel/timers/tiku_timer.h>
#if TIKU_SHELL_CMD_JOBS
#include "tiku_shell_jobs.h"
#endif
#if TIKU_SHELL_CMD_RULES
#include "tiku_shell_rules.h"
#endif

#if TIKU_SHELL_TCP_ENABLE
#include "tiku_shell_io_tcp.h"
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>  /* tiku_kits_net_process */
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND HEADERS                                                           */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_PS
#include "commands/tiku_shell_cmd_ps.h"
#endif
#if TIKU_SHELL_CMD_INFO
#include "commands/tiku_shell_cmd_info.h"
#endif
#if TIKU_SHELL_CMD_TIMER
#include "commands/tiku_shell_cmd_timer.h"
#endif
#if TIKU_SHELL_CMD_KILL
#include "commands/tiku_shell_cmd_kill.h"
#endif
#if TIKU_SHELL_CMD_RESUME
#include "commands/tiku_shell_cmd_resume.h"
#endif
#if TIKU_SHELL_CMD_QUEUE
#include "commands/tiku_shell_cmd_queue.h"
#endif
#if TIKU_SHELL_CMD_REBOOT
#include "commands/tiku_shell_cmd_reboot.h"
#endif
#if TIKU_SHELL_CMD_HISTORY
#include "commands/tiku_shell_cmd_history.h"
#endif
#if TIKU_SHELL_CMD_INIT
#include "commands/tiku_shell_cmd_init.h"
#endif
#if TIKU_SHELL_CMD_LS
#include "commands/tiku_shell_cmd_ls.h"
#endif
#if TIKU_SHELL_CMD_CD
#include "commands/tiku_shell_cmd_cd.h"
#endif
#if TIKU_SHELL_CMD_TOGGLE
#include "commands/tiku_shell_cmd_toggle.h"
#endif
#if TIKU_SHELL_CMD_START
#include "commands/tiku_shell_cmd_start.h"
#endif
#if TIKU_SHELL_CMD_WRITE
#include "commands/tiku_shell_cmd_write.h"
#endif
#if TIKU_SHELL_CMD_READ
#include "commands/tiku_shell_cmd_read.h"
#endif
#if TIKU_SHELL_CMD_WATCH
#include "commands/tiku_shell_cmd_watch.h"
#endif
#if TIKU_SHELL_CMD_CALC
#include "commands/tiku_shell_cmd_calc.h"
#endif
#if TIKU_SHELL_CMD_JOBS
#include "commands/tiku_shell_cmd_every.h"
#include "commands/tiku_shell_cmd_once.h"
#include "commands/tiku_shell_cmd_jobs.h"
#endif
#if TIKU_SHELL_CMD_RULES
#include "commands/tiku_shell_cmd_on.h"
#include "commands/tiku_shell_cmd_rules.h"
#endif
#if TIKU_SHELL_CMD_CHANGED
#include "commands/tiku_shell_cmd_changed.h"
#endif
#if TIKU_SHELL_CMD_NAME
#include "commands/tiku_shell_cmd_name.h"
#endif
#if TIKU_SHELL_CMD_IF
#include "commands/tiku_shell_cmd_if.h"
#endif
#if TIKU_SHELL_CMD_IRQ
#include "commands/tiku_shell_cmd_irq.h"
#endif
#if TIKU_SHELL_CMD_I2C
#include "commands/tiku_shell_cmd_i2c.h"
#endif
#if TIKU_SHELL_CMD_TREE
#include "commands/tiku_shell_cmd_tree.h"
#endif
#if TIKU_SHELL_CMD_CLEAR
#include "commands/tiku_shell_cmd_clear.h"
#endif
#if TIKU_SHELL_CMD_DELAY
#include "commands/tiku_shell_cmd_delay.h"
#endif
#if TIKU_SHELL_CMD_REPEAT
#include "commands/tiku_shell_cmd_repeat.h"
#endif
#if TIKU_SHELL_CMD_PEEK || TIKU_SHELL_CMD_POKE
#include "commands/tiku_shell_cmd_mem.h"
#endif
#if TIKU_SHELL_CMD_ECHO
#include "commands/tiku_shell_cmd_echo.h"
#endif
#if TIKU_SHELL_CMD_ALIAS
#include "commands/tiku_shell_cmd_alias.h"
#include "commands/tiku_shell_cmd_unalias.h"
#include "tiku_shell_alias.h"
#endif
#if TIKU_SHELL_CMD_GPIO
#include "commands/tiku_shell_cmd_gpio.h"
#endif
#if TIKU_SHELL_CMD_ADC
#include "commands/tiku_shell_cmd_adc.h"
#endif
#if TIKU_SHELL_CMD_FREE
#include "commands/tiku_shell_cmd_free.h"
#endif
#if TIKU_SHELL_CMD_SLEEP
#include "commands/tiku_shell_cmd_sleep.h"
#endif
#if TIKU_SHELL_CMD_WAKE
#include "commands/tiku_shell_cmd_wake.h"
#endif

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_HELP
static void tiku_shell_cmd_help(uint8_t argc, const char *argv[]);
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND TABLE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Static command table (NULL-terminated sentinel)
 *
 * To add a new command:
 *   1. Create the handler in apps/cli/commands/tiku_shell_cmd_xxx.c
 *   2. Add a TIKU_SHELL_CMD_XXX flag to tiku_shell_config.h
 *   3. #include the header above and add an entry here
 *   4. Add the .c to the Makefile (APP=cli section)
 */
/*
 * Category headers use handler == NULL.  The "help" command prints
 * them as section titles; the parser skips them during dispatch.
 */
#define CMD_CATEGORY(label)  { label, NULL, NULL }

static const tiku_shell_cmd_t tiku_shell_commands[] = {
    /* ---- System ---- */
    CMD_CATEGORY("System"),
#if TIKU_SHELL_CMD_HELP
    {"help",    "Show available commands",     tiku_shell_cmd_help},
#endif
#if TIKU_SHELL_CMD_INFO
    {"info",    "Device, CPU, uptime, clock",  tiku_shell_cmd_info},
#endif
#if TIKU_SHELL_CMD_FREE
    {"free",    "Memory usage (SRAM/FRAM)",    tiku_shell_cmd_free},
#endif
#if TIKU_SHELL_CMD_REBOOT
    {"reboot",  "System reset",                tiku_shell_cmd_reboot},
#endif
#if TIKU_SHELL_CMD_HISTORY
    {"history", "Last N commands from FRAM",   tiku_shell_cmd_history},
#endif
#if TIKU_SHELL_CMD_CALC
    {"calc",    "Integer arithmetic",          tiku_shell_cmd_calc},
#endif
#if TIKU_SHELL_CMD_CLEAR
    {"clear",   "Clear screen (ANSI)",         tiku_shell_cmd_clear},
#endif
#if TIKU_SHELL_CMD_DELAY
    {"delay",   "Wait <ms> (no LPM)",          tiku_shell_cmd_delay},
#endif
#if TIKU_SHELL_CMD_REPEAT
    {"repeat",  "Run command N times",         tiku_shell_cmd_repeat},
#endif

    /* ---- Processes ---- */
    CMD_CATEGORY("Processes"),
#if TIKU_SHELL_CMD_PS
    {"ps",      "List active processes",       tiku_shell_cmd_ps},
#endif
#if TIKU_SHELL_CMD_START
    {"start",   "Start/resume by name",        tiku_shell_cmd_start},
#endif
#if TIKU_SHELL_CMD_KILL
    {"kill",    "Stop a process (by pid)",     tiku_shell_cmd_kill},
#endif
#if TIKU_SHELL_CMD_RESUME
    {"resume",  "Resume a stopped process",    tiku_shell_cmd_resume},
#endif
#if TIKU_SHELL_CMD_QUEUE
    {"queue",   "List pending events",         tiku_shell_cmd_queue},
#endif
#if TIKU_SHELL_CMD_TIMER
    {"timer",   "Software timer status",       tiku_shell_cmd_timer},
#endif
#if TIKU_SHELL_CMD_JOBS
    {"every",   "Schedule a recurring command", tiku_shell_cmd_every},
    {"once",    "Schedule a one-shot command", tiku_shell_cmd_once},
    {"jobs",    "List/delete scheduled jobs",  tiku_shell_cmd_jobs},
#endif
#if TIKU_SHELL_CMD_RULES
    {"on",      "Register a reactive rule",    tiku_shell_cmd_on},
    {"rules",   "List/delete reactive rules",  tiku_shell_cmd_rules},
#endif

    /* ---- Filesystem ---- */
    CMD_CATEGORY("Filesystem"),
#if TIKU_SHELL_CMD_LS
    {"ls",      "List directory",              tiku_shell_cmd_ls},
#endif
#if TIKU_SHELL_CMD_TREE
    {"tree",    "Recursive directory listing", tiku_shell_cmd_tree},
#endif
#if TIKU_SHELL_CMD_CD
    {"cd",      "Change directory",            tiku_shell_cmd_cd},
    {"pwd",     "Print working directory",     tiku_shell_cmd_pwd},
#endif
#if TIKU_SHELL_CMD_READ
    {"read",    "Read a VFS node",             tiku_shell_cmd_read},
#endif
#if TIKU_SHELL_CMD_WATCH
    {"watch",   "Read VFS node every N sec",   tiku_shell_cmd_watch},
#endif
#if TIKU_SHELL_CMD_CHANGED
    {"changed", "Block until VFS node changes", tiku_shell_cmd_changed},
#endif
#if TIKU_SHELL_CMD_WRITE
    {"write",   "Write a VFS node",            tiku_shell_cmd_write},
#endif
#if TIKU_SHELL_CMD_NAME
    {"name",    "Read/set device name",        tiku_shell_cmd_name},
#endif
#if TIKU_SHELL_CMD_IF
    {"if",      "Conditional: if <path> <op> <value> <cmd>",
                                               tiku_shell_cmd_if},
#endif
#if TIKU_SHELL_CMD_IRQ
    {"irq",     "Enable/disable GPIO edge IRQ", tiku_shell_cmd_irq},
#endif
#if TIKU_SHELL_CMD_ALIAS
    {"alias",   "Define/list FRAM-backed aliases",
                                               tiku_shell_cmd_alias},
    {"unalias", "Remove an alias",             tiku_shell_cmd_unalias},
#endif
#if TIKU_SHELL_CMD_TOGGLE
    {"toggle",  "Flip a binary VFS node",      tiku_shell_cmd_toggle},
#endif
#if TIKU_SHELL_CMD_CAT && TIKU_SHELL_CMD_READ
    {"cat",     "Read (alias)",                tiku_shell_cmd_read},
#endif
#if TIKU_SHELL_CMD_ECHO
    {"echo",    "Print arguments + newline",   tiku_shell_cmd_echo},
#endif

    /* ---- Hardware ---- */
    CMD_CATEGORY("Hardware"),
#if TIKU_SHELL_CMD_GPIO
    {"gpio",    "Read/write GPIO pins",        tiku_shell_cmd_gpio},
#endif
#if TIKU_SHELL_CMD_ADC
    {"adc",     "Read analog channel",         tiku_shell_cmd_adc},
#endif
#if TIKU_SHELL_CMD_I2C
    {"i2c",     "I2C scan/read/write",         tiku_shell_cmd_i2c},
#endif
#if TIKU_SHELL_CMD_PEEK
    {"peek",    "Read N bytes from address",   tiku_shell_cmd_peek},
#endif
#if TIKU_SHELL_CMD_POKE
    {"poke",    "Write byte to address",       tiku_shell_cmd_poke},
#endif

    /* ---- Power ---- */
    CMD_CATEGORY("Power"),
#if TIKU_SHELL_CMD_SLEEP
    {"sleep",   "Set low-power idle mode",     tiku_shell_cmd_sleep},
#endif
#if TIKU_SHELL_CMD_WAKE
    {"wake",    "Show active wake sources",    tiku_shell_cmd_wake},
#endif

    /* ---- Boot ---- */
#if TIKU_SHELL_CMD_INIT
    CMD_CATEGORY("Boot"),
    {"init",    "Manage FRAM boot entries",    tiku_shell_cmd_init},
#endif

    {NULL, NULL, NULL}
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return a pointer to the shell command table.
 *
 * The table is a NULL-terminated array of tiku_shell_cmd_t entries.
 * Entries with handler == NULL are category headers used by the
 * "help" command for grouping.
 *
 * @return Pointer to the first element of the static command table.
 */
const tiku_shell_cmd_t *
tiku_shell_get_commands(void)
{
    return tiku_shell_commands;
}

/*---------------------------------------------------------------------------*/
/* BUILT-IN COMMANDS                                                         */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_HELP
/**
 * @brief "help" — print commands grouped by category.
 *
 * Category headers are table entries with handler == NULL.
 * The name field holds the category label.
 */
static void
tiku_shell_cmd_help(uint8_t argc, const char *argv[])
{
    const tiku_shell_cmd_t *cmd;

    (void)argc;
    (void)argv;

    for (cmd = tiku_shell_commands; cmd->name != NULL; cmd++) {
        if (cmd->handler == NULL) {
            /* Category header */
            SHELL_PRINTF(SH_CYAN " --- %s ---" SH_RST "\n", cmd->name);
        } else {
            SHELL_PRINTF("  " SH_BOLD "%-10s" SH_RST " %s\n",
                         cmd->name, cmd->help);
        }
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* CLI PROCESS                                                               */
/*---------------------------------------------------------------------------*/

/** CLI process state (static — no dynamic allocation) */
static struct {
    char               buf[TIKU_SHELL_LINE_SIZE];
    uint8_t            pos;
    struct tiku_timer  timer;    /* periodic I/O poll */
} cli;

/** The CLI shell process (registered as "CLI" in the process table). */
TIKU_PROCESS(tiku_shell_process, "CLI");

/**
 * @brief Shell process protothread — line editor and command dispatcher.
 *
 * This protothread runs as a cooperative TikuOS process.  On each
 * timer tick it drains available characters from the active I/O
 * backend, performs line editing (echo, backspace), and on CR/LF
 * dispatches the completed line to tiku_shell_parser_execute().
 *
 * Boot sequence:
 *   1. Initialise the parser with the command table.
 *   2. Set the I/O backend (UART or TCP).
 *   3. Print the boot banner and first prompt.
 *   4. Enter the poll loop — TIKU_PROCESS_WAIT_EVENT_UNTIL(timer).
 *
 * The poll timer is set to TIKU_SHELL_POLL_TICKS (typically 1 tick)
 * and re-armed at the top of every iteration so that commands which
 * inspect /sys/timer/count see it as active during execution.
 */
TIKU_PROCESS_THREAD(tiku_shell_process, ev, data)
{
    int ch;

    (void)data;

    TIKU_PROCESS_BEGIN();

    /* ---- One-time init ---- */
    tiku_shell_parser_init(tiku_shell_commands);
#if TIKU_SHELL_CMD_ALIAS
    tiku_shell_alias_init();
#endif
#if TIKU_SHELL_CMD_JOBS
    tiku_shell_jobs_init();
#endif
#if TIKU_SHELL_CMD_RULES
    tiku_shell_rules_init();
#endif
    cli.pos = 0;

#if TIKU_SHELL_TCP_ENABLE
    tiku_shell_io_tcp_init();
    /* Banner deferred until a TCP client connects (see loop below) */
#else
    tiku_shell_io_set_backend(&tiku_shell_io_uart);
    SHELL_PRINTF("\n");
    SHELL_PRINTF(SH_CYAN SH_BOLD);
    SHELL_PRINTF("  ___ _ _         ___  ___\n");
    SHELL_PRINTF(" |_ _|_) |_ _  _/ _ \\/ __|\n");
    SHELL_PRINTF("  | || | / / || | (_) \\__ \\\n");
    SHELL_PRINTF("  |_||_|_\\_\\\\_,_|\\___/|___/");
    SHELL_PRINTF(SH_RST SH_DIM "  v%s\n", TIKU_VERSION);
    SHELL_PRINTF("  %s" SH_RST "\n", TIKU_TAGLINE);
    SHELL_PRINTF("\n");
    SHELL_PRINTF("  " SH_BOLD "%s" SH_RST "  |  SRAM %luB  FRAM %luKB\n",
                 TIKU_DEVICE_NAME,
                 (unsigned long)TIKU_DEVICE_RAM_SIZE,
                 (unsigned long)(TIKU_DEVICE_FRAM_SIZE / 1024));
    SHELL_PRINTF(SH_DIM "  Type 'help' for commands." SH_RST "\n\n");
    SHELL_PRINTF(SH_GREEN SH_BOLD "tikuOS> " SH_RST);
#endif

    tiku_timer_set_event(&cli.timer, TIKU_SHELL_POLL_TICKS);

    /* ---- Main loop ---- */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

#if TIKU_SHELL_TCP_ENABLE
        /* --- TCP connection lifecycle --- */
        if (!tiku_shell_io_tcp_is_connected()) {
            /* Not connected — if we were, clear the backend */
            if (tiku_shell_io_get_backend() == &tiku_shell_io_tcp) {
                tiku_shell_io_set_backend((void *)0);
                cli.pos = 0;
            }
            tiku_timer_reset(&cli.timer);
            continue;
        }
        /* New connection arrived — install backend and show banner */
        if (tiku_shell_io_get_backend() != &tiku_shell_io_tcp) {
            tiku_shell_io_set_backend(&tiku_shell_io_tcp);
            cli.pos = 0;
            SHELL_PRINTF("\n");
            SHELL_PRINTF(SH_CYAN SH_BOLD);
            SHELL_PRINTF("  ___ _ _         ___  ___\n");
            SHELL_PRINTF(" |_ _|_) |_ _  _/ _ \\/ __|\n");
            SHELL_PRINTF("  | || | / / || | (_) \\__ \\\n");
            SHELL_PRINTF("  |_||_|_\\_\\\\_,_|\\___/|___/");
            SHELL_PRINTF(SH_RST SH_DIM "  v%s\n", TIKU_VERSION);
            SHELL_PRINTF("  %s" SH_RST "\n", TIKU_TAGLINE);
            SHELL_PRINTF("\n");
            SHELL_PRINTF("  " SH_BOLD "%s" SH_RST "  |  Telnet Shell\n",
                         TIKU_DEVICE_NAME);
            SHELL_PRINTF(SH_DIM "  Type 'help' for commands." SH_RST "\n\n");
            SHELL_PRINTF(SH_GREEN SH_BOLD "tikuOS> " SH_RST);
            tiku_shell_io_tcp_flush();
        }
#endif

        /* Re-arm poll timer first so commands that inspect
         * /sys/timer/count see it as active during execution. */
        tiku_timer_reset(&cli.timer);

        /* Drain all available characters from the backend */
        while (tiku_shell_io_rx_ready()) {
            ch = tiku_shell_io_getc();
            if (ch < 0) {
                break;
            }

            if (ch == '\r' || ch == '\n') {
                /* End of line — parse and dispatch */
                SHELL_PRINTF("\n");
                cli.buf[cli.pos] = '\0';
                if (cli.pos > 0) {
#if TIKU_SHELL_CMD_HISTORY
                    tiku_shell_history_record(cli.buf);
#endif
                    tiku_shell_parser_execute(cli.buf);
                }
                cli.pos = 0;
                SHELL_PRINTF(SH_GREEN SH_BOLD "tikuOS> " SH_RST);

            } else if (ch == '\b' || ch == 127) {
                /* Backspace */
                if (cli.pos > 0) {
                    cli.pos--;
                    if (tiku_shell_io_has_echo()) {
                        SHELL_PRINTF("\b \b");
                    }
                }

            } else if (cli.pos < TIKU_SHELL_LINE_SIZE - 1) {
                /* Printable character — store and optionally echo */
                cli.buf[cli.pos++] = (char)ch;
                if (tiku_shell_io_has_echo()) {
                    tiku_shell_io_putc((char)ch);
                }
            }
        }

#if TIKU_SHELL_CMD_JOBS
        /* Fire any due scheduled jobs.  This runs after the input
         * drain so that user keystrokes get processed first; jobs
         * dispatch through the same parser as interactive commands. */
        tiku_shell_jobs_tick();
#endif
#if TIKU_SHELL_CMD_RULES
        /* Re-evaluate reactive rules.  Edge-triggered, so actions
         * fire only on a false->true transition. */
        tiku_shell_rules_tick();
#endif

#if TIKU_SHELL_TCP_ENABLE
        tiku_shell_io_tcp_flush();
#endif
    }

    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* SHELL INIT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise and start the shell kernel service.
 *
 * Registers the CLI process (and optionally the network process
 * when TCP shell is enabled) with the TikuOS scheduler.  Call once
 * from main() after tiku_vfs_tree_init().
 *
 * The shell process prints the boot banner, starts the I/O poll
 * timer, and begins accepting commands on the next scheduler tick.
 */
void tiku_shell_init(void)
{
    tiku_process_register("Shell", &tiku_shell_process);
#if TIKU_SHELL_TCP_ENABLE
    extern struct tiku_process tiku_kits_net_process;
    tiku_process_register("Net", &tiku_kits_net_process);
#endif
}
