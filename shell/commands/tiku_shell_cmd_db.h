/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_db.h - "db": tables in place on the board's NVM.
 *
 * One verb per database operation; rows print one per line, tab-separated,
 * sequence number first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DB_H_
#define TIKU_SHELL_CMD_DB_H_

#include <stdint.h>

/**
 * @brief "db" command handler.
 *
 * @param argc Argument count
 * @param argv Argument vector: a verb and its arguments
 */
void tiku_shell_cmd_db(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DB_H_ */
