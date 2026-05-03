/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.c - Tiku BASIC interpreter engine (orchestrator).
 *
 * The interpreter is treated as a complex extension of the kernel
 * shell rather than a single shell command.  It owns ~5 KB of
 * source split across 22 themed pieces (one .inl per concern), all
 * amalgamated into this single translation unit by the chain of
 * #include directives below.
 *
 * The amalgamation keeps file-static identifiers private to BASIC,
 * lets the LTO pass see across the whole engine, and avoids the
 * boilerplate of a separate header per piece.  The order of
 * #includes follows the natural dependency chain:
 *
 *   config / state                  - tunables, types, globals
 *   arena / persist / VFS bridge    - memory + FRAM persistence
 *   peek-poke / hardware / PRNG     - low-level helpers
 *   trig                            - SIN / COS LUT
 *   lex / I/O                       - lexical helpers, line reader
 *   string / call / expr            - expression parsers
 *   program                         - the line table
 *   stmt / multi-IF / RENUM / slots - statement executors
 *   dispatch                        - exec_if + the keyword switch
 *   run / repl / shell              - run loop + REPL + entry points
 *
 * The public API is declared in tiku_basic.h.  The shell command
 * `basic` lives in kernel/shell/commands/tiku_shell_cmd_basic.{c,h}
 * as a thin dispatch stub over these entry points.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_basic.h"
#include "tiku_basic_config.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>
#include <stdio.h>
#include <string.h>

/* Hardware-bridge headers.  Each is pulled in only when the matching
 * BASIC bridge is enabled, so a slim BASIC build (e.g. no GPIO, no
 * I2C) doesn't drag in unused HAL code. */
#if TIKU_BASIC_GPIO_ENABLE && defined(PLATFORM_MSP430)
#include <arch/msp430/tiku_gpio_arch.h>
#endif
#if TIKU_BASIC_ADC_ENABLE && defined(PLATFORM_MSP430)
#include <interfaces/adc/tiku_adc.h>
#endif
#if TIKU_BASIC_I2C_ENABLE && defined(PLATFORM_MSP430)
#include <interfaces/bus/tiku_i2c_bus.h>
#endif
#if TIKU_BASIC_REBOOT_ENABLE && defined(PLATFORM_MSP430)
#include <kernel/cpu/tiku_watchdog.h>
#endif
#if TIKU_BASIC_LED_ENABLE
#include <interfaces/led/tiku_led.h>
#endif
#if TIKU_BASIC_VFS_ENABLE
#include <kernel/vfs/tiku_vfs.h>
#include <stdlib.h>     /* strtol for VFSREAD value parsing */
#endif

/*---------------------------------------------------------------------------*/
/* AMALGAMATION                                                              */
/*---------------------------------------------------------------------------*/

#include "tiku_basic_state.inl"
#include "tiku_basic_arena.inl"
#include "tiku_basic_persist.inl"
#include "tiku_basic_vfs_file.inl"
#include "tiku_basic_peek_poke.inl"
#include "tiku_basic_hw.inl"
#include "tiku_basic_prng.inl"
#include "tiku_basic_trig.inl"
#include "tiku_basic_lex.inl"
#include "tiku_basic_io.inl"
#include "tiku_basic_string.inl"
#include "tiku_basic_call.inl"
#include "tiku_basic_expr.inl"
#include "tiku_basic_program.inl"
#include "tiku_basic_stmt.inl"
#include "tiku_basic_multi_if.inl"
#include "tiku_basic_renum.inl"
#include "tiku_basic_named_slots.inl"
#include "tiku_basic_dispatch.inl"
#include "tiku_basic_run.inl"
#include "tiku_basic_repl.inl"
#include "tiku_basic_shell.inl"
