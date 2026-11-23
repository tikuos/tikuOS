/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_board.h - "board" command: rail, bus and coprocessor controls
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BOARD_H_
#define TIKU_SHELL_CMD_BOARD_H_

#include <stdint.h>

/**
 * @brief "board" command -- the controls of interfaces/board/tiku_board_ctl.h.
 *
 * With no arguments it prints the controls; `rail on|off`, `bus off|<host>|
 * <coproc>`, `<coproc> on|off|reset` and `wake 0|1` change them.
 * @note <host> and <coproc> are the board's names for its two processors; an
 *       order the board's rules forbid is refused with nothing changed.
 */
void tiku_shell_cmd_board(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BOARD_H_ */
