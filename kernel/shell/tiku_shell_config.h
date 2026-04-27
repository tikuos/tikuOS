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

/* Each flag is wrapped in #ifndef so the build system can override
 * with -DTIKU_SHELL_CMD_X=0 (or =1) on the command line via
 * EXTRA_CFLAGS.  The Makefile gates the matching .c on findstring
 * checks against EXTRA_CFLAGS so the disabled command compiles to
 * zero text. */
#ifndef TIKU_SHELL_CMD_HELP
#define TIKU_SHELL_CMD_HELP    1  /**< help    - List available commands */
#endif
#ifndef TIKU_SHELL_CMD_PS
#define TIKU_SHELL_CMD_PS      1  /**< ps      - List active processes */
#endif
#ifndef TIKU_SHELL_CMD_INFO
#define TIKU_SHELL_CMD_INFO    1  /**< info    - System overview */
#endif
#ifndef TIKU_SHELL_CMD_TIMER
#define TIKU_SHELL_CMD_TIMER   1  /**< timer   - Software timer status */
#endif
#ifndef TIKU_SHELL_CMD_KILL
#define TIKU_SHELL_CMD_KILL    1  /**< kill    - Stop a process */
#endif
#ifndef TIKU_SHELL_CMD_RESUME
#define TIKU_SHELL_CMD_RESUME  1  /**< resume  - Resume a stopped process */
#endif
#ifndef TIKU_SHELL_CMD_QUEUE
#define TIKU_SHELL_CMD_QUEUE   1  /**< queue   - List pending events */
#endif
#ifndef TIKU_SHELL_CMD_REBOOT
#define TIKU_SHELL_CMD_REBOOT  1  /**< reboot  - System reset */
#endif
#ifndef TIKU_SHELL_CMD_HISTORY
#define TIKU_SHELL_CMD_HISTORY 1  /**< history - Last N commands from FRAM */
#endif
#ifndef TIKU_SHELL_CMD_LS
#define TIKU_SHELL_CMD_LS      1  /**< ls      - List VFS directory contents */
#endif
#ifndef TIKU_SHELL_CMD_CD
#define TIKU_SHELL_CMD_CD      1  /**< cd/pwd  - Change/print working directory */
#endif
#ifndef TIKU_SHELL_CMD_TOGGLE
#define TIKU_SHELL_CMD_TOGGLE  1  /**< toggle  - Binary state flip via VFS */
#endif
#ifndef TIKU_SHELL_CMD_START
#define TIKU_SHELL_CMD_START   1  /**< start   - Launch/resume process by name */
#endif
#ifndef TIKU_SHELL_CMD_WRITE
#define TIKU_SHELL_CMD_WRITE   1  /**< write   - Write value to VFS node */
#endif
#ifndef TIKU_SHELL_CMD_READ
#define TIKU_SHELL_CMD_READ    1  /**< read    - Read value from VFS node */
#endif
#ifndef TIKU_SHELL_CMD_GPIO
#define TIKU_SHELL_CMD_GPIO    1  /**< gpio    - Direct GPIO pin control */
#endif
#ifndef TIKU_SHELL_CMD_ADC
#define TIKU_SHELL_CMD_ADC     1  /**< adc     - Read analog channels */
#endif
#ifndef TIKU_SHELL_CMD_FREE
#define TIKU_SHELL_CMD_FREE    1  /**< free    - Memory usage summary */
#endif
#ifndef TIKU_SHELL_CMD_SLEEP
#define TIKU_SHELL_CMD_SLEEP   1  /**< sleep   - Enter low-power idle mode */
#endif
#ifndef TIKU_SHELL_CMD_WAKE
#define TIKU_SHELL_CMD_WAKE    1  /**< wake    - Show active wake sources */
#endif
#ifndef TIKU_SHELL_CMD_NAME
#define TIKU_SHELL_CMD_NAME    1  /**< name    - Read or set device name */
#endif
#ifndef TIKU_SHELL_CMD_IF
#define TIKU_SHELL_CMD_IF      1  /**< if      - Conditional VFS-driven action */
#endif
#ifndef TIKU_SHELL_CMD_IRQ
#define TIKU_SHELL_CMD_IRQ     1  /**< irq     - GPIO edge interrupt -> event */
#endif
#ifndef TIKU_SHELL_CMD_ALIAS
#define TIKU_SHELL_CMD_ALIAS   1  /**< alias   - FRAM-backed shell shortcuts */
#endif
#ifndef TIKU_SHELL_CMD_CAT
#define TIKU_SHELL_CMD_CAT     1  /**< cat     - Alias for read */
#endif
#ifndef TIKU_SHELL_CMD_ECHO
#define TIKU_SHELL_CMD_ECHO    1  /**< echo    - Print arguments + newline */
#endif
#ifndef TIKU_SHELL_CMD_WATCH
#define TIKU_SHELL_CMD_WATCH   1  /**< watch   - Periodic VFS read until Ctrl+C */
#endif
#ifndef TIKU_SHELL_CMD_CALC
#define TIKU_SHELL_CMD_CALC    1  /**< calc    - Integer arithmetic */
#endif
#ifndef TIKU_SHELL_CMD_JOBS
#define TIKU_SHELL_CMD_JOBS    1  /**< jobs    - every/once/jobs */
#endif
#ifndef TIKU_SHELL_CMD_RULES
#define TIKU_SHELL_CMD_RULES   1  /**< rules   - on/rules */
#endif
#ifndef TIKU_SHELL_CMD_CHANGED
#define TIKU_SHELL_CMD_CHANGED 1  /**< changed - Block until VFS value changes */
#endif
/* I2C is opt-in: it pulls in tiku_i2c_bus and arch driver, which
 * together cost ~1.4 KB of FRAM.  The default FR5969 shell build
 * already sits at ~44 KB of the 48 KB FRAM cap, so enabling I2C
 * requires turning off something else of comparable size.  Two
 * recipes that fit comfortably:
 *
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1 -DTIKU_SHELL_CMD_HISTORY=0"
 *
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1 -DTIKU_SHELL_CMD_CALC=0"
 */
#ifndef TIKU_SHELL_CMD_I2C
#define TIKU_SHELL_CMD_I2C    0  /**< i2c    - Bus scan / read / write */
#endif
#define TIKU_SHELL_CMD_TREE   1  /**< tree   - Recursive VFS dump */
#define TIKU_SHELL_CMD_CLEAR  1  /**< clear  - ANSI clear screen */

/* Scripting and debugging extras: enabled per-build via EXTRA_CFLAGS
 * because the default FR5969 shell already sits ~250 B from the
 * 48 KB FRAM cap.  Pick the combination you need; multiple flags
 * are independent.  Example:
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_DELAY=1 -DTIKU_SHELL_CMD_REPEAT=1"
 *
 * To enable the larger ones (peek/poke/i2c), pair them with a
 * disable of a comparable existing command, e.g.
 *   EXTRA_CFLAGS="-DTIKU_SHELL_CMD_PEEK=1 -DTIKU_SHELL_CMD_POKE=1 -DTIKU_SHELL_CMD_HISTORY=0"
 */
#ifndef TIKU_SHELL_CMD_DELAY
#define TIKU_SHELL_CMD_DELAY  0  /**< delay  - Synchronous ms wait */
#endif
#ifndef TIKU_SHELL_CMD_REPEAT
#define TIKU_SHELL_CMD_REPEAT 0  /**< repeat - Run command N times */
#endif
#ifndef TIKU_SHELL_CMD_PEEK
#define TIKU_SHELL_CMD_PEEK   0  /**< peek   - Read raw memory */
#endif
#ifndef TIKU_SHELL_CMD_POKE
#define TIKU_SHELL_CMD_POKE   0  /**< poke   - Write raw memory */
#endif
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
