/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_board.c - "board" command implementation.
 *
 * The board header names its two processors (TIKU_BOARD_CTL_HOST_NAME,
 * TIKU_BOARD_CTL_COPROC_NAME), which the command takes as words.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_board.h"
#include <shell/tiku_shell.h>
#include <interfaces/board/tiku_board_ctl.h>
#include "tiku.h"

#if TIKU_SHELL_CMD_BOARD

/** @brief 1 if NUL-terminated strings @p a and @p b are equal, else 0. */
static uint8_t
board_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/** @brief Print the controls. */
static void
board_show(void)
{
    tiku_board_ctl_state_t s;

    tiku_board_ctl_state(&s);
    SHELL_PRINTF("rail  %s\n", s.rail ? "on" : "off");
    SHELL_PRINTF("bus   %s\n",
                 s.bus == TIKU_BOARD_BUS_COPROC ? TIKU_BOARD_CTL_COPROC_NAME :
                 s.bus == TIKU_BOARD_BUS_HOST   ? TIKU_BOARD_CTL_HOST_NAME :
                                                  "off");
    SHELL_PRINTF("%-5s %s%s\n", TIKU_BOARD_CTL_COPROC_NAME,
                 s.coproc_power ? "on" : "off",
                 s.coproc_reset ? ", in reset" : "");
    SHELL_PRINTF("wake  out %u, in %u\n", (unsigned)s.wake_out,
                 (unsigned)s.wake_in);
}

/** @brief Report @p rc of the call for @p what; show the controls on success. */
static void
board_done(int rc, const char *what)
{
    if (rc == TIKU_BOARD_CTL_OK) {
        board_show();
    } else if (rc == TIKU_BOARD_CTL_ERR_ORDER) {
        SHELL_PRINTF("board: %s refused: it would break a hand-over rule\n",
                     what);
    } else {
        SHELL_PRINTF("board: bad %s\n", what);
    }
}

/** @brief 1 for "on", 0 for "off", or -1. */
static int
board_onoff(const char *s)
{
    if (board_streq(s, "on")) {
        return 1;
    }
    if (board_streq(s, "off")) {
        return 0;
    }
    return -1;
}

void
tiku_shell_cmd_board(uint8_t argc, const char *argv[])
{
    int v;

    if (argc < 2) {
        board_show();
        return;
    }
    if (argc != 3) {
        SHELL_PRINTF("Usage: board [rail on|off | bus off|%s|%s | "
                     "%s on|off|reset | wake 0|1]\n",
                     TIKU_BOARD_CTL_HOST_NAME, TIKU_BOARD_CTL_COPROC_NAME,
                     TIKU_BOARD_CTL_COPROC_NAME);
        return;
    }
    if (board_streq(argv[1], "rail")) {
        v = board_onoff(argv[2]);
        if (v < 0) {
            SHELL_PRINTF("board: rail on or off\n");
            return;
        }
        board_done(tiku_board_ctl_rail((uint8_t)v), "rail");
        return;
    }
    if (board_streq(argv[1], "bus")) {
        if (board_streq(argv[2], "off")) {
            v = TIKU_BOARD_BUS_OFF;
        } else if (board_streq(argv[2], TIKU_BOARD_CTL_HOST_NAME)) {
            v = TIKU_BOARD_BUS_HOST;
        } else if (board_streq(argv[2], TIKU_BOARD_CTL_COPROC_NAME)) {
            v = TIKU_BOARD_BUS_COPROC;
        } else {
            SHELL_PRINTF("board: bus off, %s or %s\n",
                         TIKU_BOARD_CTL_HOST_NAME, TIKU_BOARD_CTL_COPROC_NAME);
            return;
        }
        board_done(tiku_board_ctl_bus((uint8_t)v), "bus");
        return;
    }
    if (board_streq(argv[1], TIKU_BOARD_CTL_COPROC_NAME)) {
        if (board_streq(argv[2], "reset")) {
            board_done(tiku_board_ctl_coproc_reset(), "reset");
            return;
        }
        v = board_onoff(argv[2]);
        if (v < 0) {
            SHELL_PRINTF("board: %s on, off or reset\n",
                         TIKU_BOARD_CTL_COPROC_NAME);
            return;
        }
        board_done(tiku_board_ctl_coproc_power((uint8_t)v), "power");
        return;
    }
    if (board_streq(argv[1], "wake")) {
        if (!board_streq(argv[2], "0") && !board_streq(argv[2], "1")) {
            SHELL_PRINTF("board: wake 0 or 1\n");
            return;
        }
        board_done(tiku_board_ctl_wake((uint8_t)(argv[2][0] - '0')), "wake");
        return;
    }
    SHELL_PRINTF("board: unknown '%s'\n", argv[1]);
}

#endif /* TIKU_SHELL_CMD_BOARD */
