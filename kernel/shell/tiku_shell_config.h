/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_config.h - CLI command selection flags
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
 * @file tiku_shell_config.h
 * @brief Enable/disable individual CLI commands
 *
 * To add a new command:
 *   1. Add a TIKU_SHELL_CMD_xxx flag here (set to 1)
 *   2. Create apps/cli/commands/tiku_shell_cmd_xxx.h and .c
 *   3. Add #include and table entry in tiku_cli.c
 *   4. Add the .c file to the Makefile APP=cli section
 */

#ifndef TIKU_SHELL_CONFIG_H_
#define TIKU_SHELL_CONFIG_H_

/** @defgroup TIKU_SHELL_CMDS CLI Command Flags
 * @brief Set to 1 to include a command, 0 to exclude.
 * @{
 */

#define TIKU_SHELL_CMD_HELP   1  /**< help   - List available commands */
#define TIKU_SHELL_CMD_PS     1  /**< ps     - List active processes */
#define TIKU_SHELL_CMD_INFO   1  /**< info   - System overview */
#define TIKU_SHELL_CMD_TIMER  1  /**< timer  - Software timer status */
#define TIKU_SHELL_CMD_KILL   1  /**< kill   - Stop a process */
#define TIKU_SHELL_CMD_RESUME 1  /**< resume - Resume a stopped process */
#define TIKU_SHELL_CMD_QUEUE   1  /**< queue   - List pending events */
#define TIKU_SHELL_CMD_REBOOT  1  /**< reboot  - System reset */
#define TIKU_SHELL_CMD_HISTORY 1  /**< history - Last N commands from FRAM */
#define TIKU_SHELL_CMD_LS     1  /**< ls     - List VFS directory contents */
#define TIKU_SHELL_CMD_CD     1  /**< cd/pwd - Change/print working directory */
#define TIKU_SHELL_CMD_TOGGLE 1  /**< toggle - Binary state flip via VFS */
#define TIKU_SHELL_CMD_START  1  /**< start  - Launch/resume process by name */
#define TIKU_SHELL_CMD_WRITE  1  /**< write  - Write value to VFS node */
#define TIKU_SHELL_CMD_READ   1  /**< read   - Read value from VFS node */
#define TIKU_SHELL_CMD_GPIO   1  /**< gpio   - Direct GPIO pin control */
#define TIKU_SHELL_CMD_ADC    1  /**< adc    - Read analog channels */
#define TIKU_SHELL_CMD_FREE   1  /**< free   - Memory usage summary */
#define TIKU_SHELL_CMD_SLEEP  1  /**< sleep  - Enter low-power idle mode */
#define TIKU_SHELL_CMD_WAKE   1  /**< wake   - Show active wake sources */
#define TIKU_SHELL_CMD_CAT    1  /**< cat    - Alias for read */
#define TIKU_SHELL_CMD_ECHO   1  /**< echo   - Alias for write */
#ifndef TIKU_SHELL_CMD_INIT
#define TIKU_SHELL_CMD_INIT    TIKU_INIT_ENABLE  /**< init - FRAM boot entries */
#endif

/** @} */

/** @defgroup TIKU_SHELL_COLOR ANSI Color Output
 * @brief Enable colored shell output via ANSI escape codes.
 *
 * Build with:  make TIKU_SHELL_COLOR=1 MCU=msp430fr5969
 *
 * Requires a terminal that renders ANSI escapes (picocom, screen,
 * minicom, PuTTY, telnet).  Disable for raw serial logging.
 * @{
 */

#ifndef TIKU_SHELL_COLOR
#define TIKU_SHELL_COLOR  0
#endif

#if TIKU_SHELL_COLOR

#define SH_RST     "\033[0m"      /**< Reset all attributes */
#define SH_BOLD    "\033[1m"      /**< Bold / bright */
#define SH_DIM     "\033[2m"      /**< Dim / faint */

#define SH_RED     "\033[31m"     /**< Red text */
#define SH_GREEN   "\033[32m"     /**< Green text */
#define SH_YELLOW  "\033[33m"     /**< Yellow text */
#define SH_BLUE    "\033[34m"     /**< Blue text */
#define SH_MAGENTA "\033[35m"     /**< Magenta text */
#define SH_CYAN    "\033[36m"     /**< Cyan text */
#define SH_WHITE   "\033[37m"     /**< White text */

#else /* !TIKU_SHELL_COLOR */

#define SH_RST     ""
#define SH_BOLD    ""
#define SH_DIM     ""
#define SH_RED     ""
#define SH_GREEN   ""
#define SH_YELLOW  ""
#define SH_BLUE    ""
#define SH_MAGENTA ""
#define SH_CYAN    ""
#define SH_WHITE   ""

#endif /* TIKU_SHELL_COLOR */

/** @} */

/** @defgroup TIKU_SHELL_BACKENDS CLI Backend Selection
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
 *        EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_SHELL_TCP_ENABLE=1"
 */
#ifndef TIKU_SHELL_TCP_ENABLE
#define TIKU_SHELL_TCP_ENABLE 0
#endif

/** @} */

#endif /* TIKU_SHELL_CONFIG_H_ */
