/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_config.h - CLI command selection flags
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

/**
 * @file tiku_cli_config.h
 * @brief Enable/disable individual CLI commands
 *
 * To add a new command:
 *   1. Add a TIKU_CLI_CMD_xxx flag here (set to 1)
 *   2. Create apps/cli/commands/tiku_cli_cmd_xxx.h and .c
 *   3. Add #include and table entry in tiku_cli.c
 *   4. Add the .c file to the Makefile APP=cli section
 */

#ifndef TIKU_CLI_CONFIG_H_
#define TIKU_CLI_CONFIG_H_

/** @defgroup TIKU_CLI_CMDS CLI Command Flags
 * @brief Set to 1 to include a command, 0 to exclude.
 * @{
 */

#define TIKU_CLI_CMD_HELP   1  /**< help   - List available commands */
#define TIKU_CLI_CMD_PS     1  /**< ps     - List active processes */
#define TIKU_CLI_CMD_INFO   1  /**< info   - System overview */
#define TIKU_CLI_CMD_TIMER  1  /**< timer  - Software timer status */
#define TIKU_CLI_CMD_KILL   1  /**< kill   - Stop a process */
#define TIKU_CLI_CMD_RESUME 1  /**< resume - Resume a stopped process */

/** @} */

/** @defgroup TIKU_CLI_BACKENDS CLI Backend Selection
 * @brief Enable optional I/O backends (UART is always available).
 * @{
 */

/**
 * @brief Enable TCP (telnet) backend on port 23.
 *
 * Requires the TikuKits TCP stack (TIKU_KITS_NET_TCP_ENABLE=1).
 * The net process must be auto-started alongside the CLI process.
 * Build with:
 *   make APP=cli MCU=msp430fr5969 \
 *        EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_CLI_TCP_ENABLE=1"
 */
#ifndef TIKU_CLI_TCP_ENABLE
#define TIKU_CLI_TCP_ENABLE 0
#endif

/** @} */

#endif /* TIKU_CLI_CONFIG_H_ */
