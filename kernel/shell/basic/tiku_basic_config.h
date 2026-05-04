/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_config.h - compile-time tunables for the Tiku BASIC
 *                       interpreter engine.
 *
 * Every macro is `#ifndef`-guarded so a build can override it
 * through -D flags or EXTRA_CFLAGS.  They control buffer sizes,
 * optional language features (string vars, DEF FN, arrays,
 * fixed-point), and which hardware bridges (GPIO / ADC / I2C / LED
 * / VFS / REBOOT) are compiled in.
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

#ifndef TIKU_BASIC_CONFIG_H_
#define TIKU_BASIC_CONFIG_H_

/*---------------------------------------------------------------------------*/
/* CORE LIMITS                                                               */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BASIC_LINE_MAX
#define TIKU_BASIC_LINE_MAX        48
#endif
#ifndef TIKU_BASIC_PROGRAM_LINES
#define TIKU_BASIC_PROGRAM_LINES   24
#endif
#ifndef TIKU_BASIC_GOSUB_DEPTH
#define TIKU_BASIC_GOSUB_DEPTH      8
#endif
#ifndef TIKU_BASIC_FOR_DEPTH
#define TIKU_BASIC_FOR_DEPTH        4
#endif
#ifndef TIKU_BASIC_LOOP_DEPTH
#define TIKU_BASIC_LOOP_DEPTH       4
#endif

/*---------------------------------------------------------------------------*/
/* OPTIONAL LANGUAGE FEATURES                                                */
/*---------------------------------------------------------------------------*/

/* Direct memory access via PEEK / POKE. Defaults on for MSP430 (the
 * intended use case: poking GPIO / peripheral registers from BASIC),
 * off everywhere else. Set to 0 in builds where giving the user
 * unrestricted memory writes is not desirable. */
#ifndef TIKU_BASIC_PEEK_POKE_ENABLE
#ifdef PLATFORM_MSP430
#define TIKU_BASIC_PEEK_POKE_ENABLE 1
#else
#define TIKU_BASIC_PEEK_POKE_ENABLE 1   /* host harness exercises this too */
#endif
#endif

/* Hardware bridges. Each can be disabled individually for ultra-thin
 * BASIC builds; defaults are on so a fresh user can blink an LED in
 * three lines. */
#ifndef TIKU_BASIC_GPIO_ENABLE
#define TIKU_BASIC_GPIO_ENABLE      1
#endif
#ifndef TIKU_BASIC_ADC_ENABLE
#define TIKU_BASIC_ADC_ENABLE       1
#endif
#ifndef TIKU_BASIC_I2C_ENABLE
#define TIKU_BASIC_I2C_ENABLE       1
#endif
#ifndef TIKU_BASIC_LED_ENABLE
#define TIKU_BASIC_LED_ENABLE       1
#endif
#ifndef TIKU_BASIC_VFS_ENABLE
#define TIKU_BASIC_VFS_ENABLE       1
#endif

/*---------------------------------------------------------------------------*/
/* MULTI-LETTER VARIABLE NAMES                                               */
/*---------------------------------------------------------------------------*/

/* Beyond the 26 single-letter A..Z slots, BASIC supports a small
 * pool of named variables (e.g. NAME, COUNT, X1).  Each slot
 * stores a name up to TIKU_BASIC_NAMEDVAR_LEN-1 chars (NUL-
 * terminated) plus the value cell, allocated lazily on first use.
 * Numeric and string named vars share the same maximum count but
 * occupy independent name tables, so MYVAR and MYVAR$ can coexist. */
#ifndef TIKU_BASIC_NAMEDVAR_MAX
#define TIKU_BASIC_NAMEDVAR_MAX     16
#endif
#ifndef TIKU_BASIC_NAMEDVAR_LEN
#define TIKU_BASIC_NAMEDVAR_LEN     8       /* 7 chars + NUL */
#endif

/*---------------------------------------------------------------------------*/
/* STRING / DEF FN / ARRAY / SLOT / FIXED-POINT TUNABLES                     */
/*---------------------------------------------------------------------------*/

/* String variables A$..Z$ + string functions. The heap is reset at
 * the start of every RUN, so a single program run is bounded by
 * TIKU_BASIC_STR_HEAP_BYTES of total string allocation. There is no
 * GC -- if you exceed the heap mid-run, the program errors. The
 * single-statement scratch buffer caps any one string-expression
 * result at TIKU_BASIC_STR_BUF_CAP bytes. */
#ifndef TIKU_BASIC_STRVARS_ENABLE
#define TIKU_BASIC_STRVARS_ENABLE   1
#endif
#ifndef TIKU_BASIC_STR_HEAP_BYTES
#define TIKU_BASIC_STR_HEAP_BYTES   512
#endif
#ifndef TIKU_BASIC_STR_BUF_CAP
#define TIKU_BASIC_STR_BUF_CAP      64
#endif

/* DEF FN single-line user functions. Each entry stores a name (up
 * to 7 chars), the argument-letter index, and an expression body
 * up to TIKU_BASIC_DEFN_BODY chars long. Calls evaluate the body
 * with the call-site argument bound to the named variable. */
#ifndef TIKU_BASIC_DEFN_ENABLE
#define TIKU_BASIC_DEFN_ENABLE      1
#endif
#ifndef TIKU_BASIC_DEFN_MAX
#define TIKU_BASIC_DEFN_MAX         4
#endif
#ifndef TIKU_BASIC_DEFN_BODY
#define TIKU_BASIC_DEFN_BODY        40
#endif

/* DIM A(n) integer arrays. 26 slots (one per A..Z); each starts
 * unallocated. DIM bumps an arena allocation. Re-DIMming the same
 * variable in a single session is rejected. Arrays are zero-filled
 * at every RUN start to match basic_vars semantics. */
#ifndef TIKU_BASIC_ARRAYS_ENABLE
#define TIKU_BASIC_ARRAYS_ENABLE    1
#endif
#ifndef TIKU_BASIC_ARRAY_MAX
#define TIKU_BASIC_ARRAY_MAX        128
#endif

/* Multi-slot SAVE / LOAD. The default unnamed SAVE / LOAD continue
 * to use the original "prog" persist key; named slots live in
 * their own .persistent table. */
#ifndef TIKU_BASIC_NAMED_SLOTS
#define TIKU_BASIC_NAMED_SLOTS      3
#endif
#ifndef TIKU_BASIC_NAMED_SLOT_BYTES
#define TIKU_BASIC_NAMED_SLOT_BYTES 192
#endif

/* Fixed-point math.  Numbers with a decimal point in source (`1.5`,
 * `0.001`, ...) are scaled by TIKU_BASIC_FIXED_SCALE on parse and
 * stored as plain integers. The default scale of 1000 matches the
 * existing PI = 3142 (= 3.142 * 1000) convention; this lets FMUL,
 * FDIV, and FSTR$ all share that same Q.3 base. Operations:
 *
 *    +, -                    work directly on Q.3 integers
 *    a * pure_int            works directly
 *    a / pure_int            works directly (truncates)
 *    FMUL(a, b)              a * b / SCALE  (true fixed-point mul)
 *    FDIV(a, b)              a * SCALE / b  (true fixed-point div)
 *    FSTR$(x)                "1.500" -- stringify with the decimal
 *
 * Intermediate products use 64-bit (long long) so values up to a few
 * thousand multiply safely without overflow. */
#ifndef TIKU_BASIC_FIXED_ENABLE
#define TIKU_BASIC_FIXED_ENABLE     1
#endif
#ifndef TIKU_BASIC_FIXED_SCALE
#define TIKU_BASIC_FIXED_SCALE      1000L
#endif

/* REBOOT triggers the watchdog and spins; only meaningful on real
 * silicon. Defaults off in host builds because the spin would hang
 * the test driver. */
#ifndef TIKU_BASIC_REBOOT_ENABLE
#ifdef PLATFORM_MSP430
#define TIKU_BASIC_REBOOT_ENABLE    1
#else
#define TIKU_BASIC_REBOOT_ENABLE    0
#endif
#endif

/* Pi as a Q.3 fixed-point literal (3.142). Lets the user do
 * coarse-grained trig-style math without floats: e.g.
 * `LET CIRC = D * PI / 1000`. */
#ifndef TIKU_BASIC_PI_Q3
#define TIKU_BASIC_PI_Q3            3142
#endif

/* FRAM serialization buffer for the saved program. Sized to fit the
 * worst-case stored representation: every line at full length plus
 * a 5-digit line number, a separator, and a newline. */
#ifndef TIKU_BASIC_SAVE_BUF_BYTES
#define TIKU_BASIC_SAVE_BUF_BYTES \
    ((tiku_mem_arch_size_t)(TIKU_BASIC_PROGRAM_LINES * \
                             (TIKU_BASIC_LINE_MAX + 8u)))
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

#define BASIC_CTRL_C       0x03
#define BASIC_PERSIST_KEY  "prog"

#ifdef PLATFORM_MSP430
#define BASIC_NVM_PERSISTENT __attribute__((section(".persistent")))
#else
#define BASIC_NVM_PERSISTENT
#endif

#endif /* TIKU_BASIC_CONFIG_H_ */
