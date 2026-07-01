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
/* MEMORY TIER -- drives the default limits below                            */
/*---------------------------------------------------------------------------*/

/* The interpreter's entire working set (line table, variables, control-flow
 * stacks, string heap, arrays) is allocated from ONE kernel arena, sized from
 * the limits below (see tiku_basic_arena.inl) and drawn from the AUTO memory
 * tier.  So a bigger limit just asks the arena for more -- it does NOT cost
 * static BSS (only ~50 bytes of pointers are fixed).  The only static cost that
 * scales is the FRAM/MRAM/flash save buffer (= PROGRAM_LINES * (LINE_MAX + 8)).
 *
 * The sensible default therefore depends on how much that tier can give, which
 * is a property of the board.  Three tiers:
 *
 *   BIG  - Apollo (3 MB / 384 KB SSRAM) and RP2350 (520 KB SRAM): generous.
 *   FRAM - MSP430 FR5994/FR6989 with MEMORY_MODEL=large: the arena routes to
 *          HIFRAM (256 KB FRAM), so the 8 KB SRAM is not the bound -> roomy.
 *   else - FR5969 (2 KB SRAM) and the host test harness: the original lean
 *          limits, left EXACTLY as they were.
 *
 * Every macro stays -D-overridable; these branches only choose the default. */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350)
#define TIKU_BASIC_TIER_BIG  1
#elif defined(TIKU_MEMORY_MODEL_LARGE)
#define TIKU_BASIC_TIER_FRAM 1
#endif

/* Apollo510 (Apollo5) has the most RAM of the BIG-class parts -- 512 KB TCM
 * plus 3 MB SSRAM -- so it gets the largest PROGRAM_LINES.  It is also
 * PLATFORM_AMBIQ, so it already inherits every BIG secondary limit above; only
 * the program size differs from Apollo4 Lite / RP2350. */
#if defined(AM_PART_APOLLO510)
#define TIKU_BASIC_TIER_HUGE 1
#endif

/*---------------------------------------------------------------------------*/
/* CORE LIMITS                                                               */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BASIC_LINE_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_LINE_MAX     80
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_LINE_MAX     64
#  else
#    define TIKU_BASIC_LINE_MAX     48
#  endif
#endif

/* PROGRAM_LINES is the one limit with a static cost that scales: the save
 * buffer (basic_save_buf) is PROGRAM_LINES * (LINE_MAX + 8) bytes, and on
 * non-MSP430 parts it is plain .bss (the .persistent attribute is MSP430-only),
 * so at LINE_MAX=80 that is ~88 bytes of always-on RAM per line:
 *   HUGE  2048 -> ~180 KB .bss (Apollo510: 512 KB TCM + 3 MB SSRAM)
 *   BIG   1024 ->  ~90 KB .bss (Apollo4 Lite 1 MB SSRAM / RP2350 520 KB)
 *   FRAM   256 -> ~18 KB FRAM .persistent (FR5994/6989, 256 KB FRAM)
 *   else    50 ->  ~3 KB (host harness / small)
 * Line numbers are uint16_t, so the hard ceiling is 65533 lines. */
#ifndef TIKU_BASIC_PROGRAM_LINES
#  if defined(TIKU_BASIC_TIER_HUGE)
#    define TIKU_BASIC_PROGRAM_LINES 2048
#  elif defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_PROGRAM_LINES 1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_PROGRAM_LINES 256
#  else
#    define TIKU_BASIC_PROGRAM_LINES 50
#  endif
#endif

#ifndef TIKU_BASIC_GOSUB_DEPTH       /* sp is uint8_t -> hard max 255 */
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_GOSUB_DEPTH   32
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_GOSUB_DEPTH   16
#  else
#    define TIKU_BASIC_GOSUB_DEPTH    8
#  endif
#endif

#ifndef TIKU_BASIC_FOR_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FOR_DEPTH     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_FOR_DEPTH      8
#  else
#    define TIKU_BASIC_FOR_DEPTH      4
#  endif
#endif

#ifndef TIKU_BASIC_LOOP_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_LOOP_DEPTH    16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_LOOP_DEPTH     8
#  else
#    define TIKU_BASIC_LOOP_DEPTH     4
#  endif
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
/* FULL-PROFILE FEATURE GATES                                                */
/*---------------------------------------------------------------------------*/

/* These default ON for the resource-rich Cortex-M parts (RP2350 / Apollo4 /
 * Apollo510 -- all TIER_BIG) and OFF for the FRAM/small parts (MSP430), giving
 * a "lite" BASIC on MSP430 and a "full" BASIC on the bigger controllers. Each
 * is still -D-overridable. Several also need their kit compiled in (the gate
 * encodes that dependency) so an enabled feature can never dangle at link.
 *
 *   RTC   : NOW / DATE$ / TIME$ / SETTIME wall-clock (DATE$/TIME$ also need
 *           TIKU_KIT_TIME_ENABLE for the calendar breakdown -- gated in-file).
 *   MATHX : LOG / EXP / POW / ATAN + the '^' operator (fixed-point).
 *   FILE  : APPEND / FWRITE / FREAD$ logging to /data (via the VFS).
 *   NET   : UDPSEND / MQTTPUB / HTTPGET$ -- requires the net kit.
 *   SUBS  : multi-line SUB / FUNCTION / LOCAL / CALL with call frames. */
#ifndef TIKU_BASIC_RTC_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_RTC_ENABLE    1
#  else
#    define TIKU_BASIC_RTC_ENABLE    0
#  endif
#endif
#ifndef TIKU_BASIC_MATHX_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_MATHX_ENABLE  1
#  else
#    define TIKU_BASIC_MATHX_ENABLE  0
#  endif
#endif
#ifndef TIKU_BASIC_FILE_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FILE_ENABLE   1
#  else
#    define TIKU_BASIC_FILE_ENABLE   0
#  endif
#endif
#ifndef TIKU_BASIC_NET_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_NET_ENABLE + 0)
#    define TIKU_BASIC_NET_ENABLE    1
#  else
#    define TIKU_BASIC_NET_ENABLE    0
#  endif
#endif
/* JSON$ -- extract a value by dotted path (keys + array indices) from a JSON
 * string; the agent primitive for parsing API/LLM replies. Wraps the
 * codec/json pull-parser, so it needs TIKU_KIT_CODEC_ENABLE (which compiles
 * tikukits/codec/json). BIG-only. */
#ifndef TIKU_BASIC_JSON_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_CODEC_ENABLE + 0)
#    define TIKU_BASIC_JSON_ENABLE   1
#  else
#    define TIKU_BASIC_JSON_ENABLE   0
#  endif
#endif
#ifndef TIKU_BASIC_SUBS_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_SUBS_ENABLE   1
#  else
#    define TIKU_BASIC_SUBS_ENABLE   0
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* HTTP REQUEST ASSEMBLY BUDGET (HTTPGET$ / HTTPPOST$)                        */
/*---------------------------------------------------------------------------*/

/* Single source of truth for the bounded inputs basic_https_get() concatenates
 * into its request buffer.  The caller (tiku_basic_string.inl) sizes its
 * host/path/content-type buffers from these; HTTPHEADER bounds its block to
 * TIKU_BASIC_HTTP_HDRS_MAX; and a _Static_assert in tiku_basic_https.inl proves
 * the worst-case assembled request still fits TIKU_BASIC_HTTP_REQ_MAX.  So the
 * req[] budget is enforced at compile time across all three files -- bumping any
 * cap without growing REQ_MAX breaks the build instead of overflowing req[]. */
#ifndef TIKU_BASIC_HTTP_HOST_MAX
#define TIKU_BASIC_HTTP_HOST_MAX    64
#endif
#ifndef TIKU_BASIC_HTTP_PATH_MAX
#define TIKU_BASIC_HTTP_PATH_MAX    80
#endif
#ifndef TIKU_BASIC_HTTP_CTYPE_MAX
#define TIKU_BASIC_HTTP_CTYPE_MAX   48
#endif
#ifndef TIKU_BASIC_HTTP_HDRS_MAX
#define TIKU_BASIC_HTTP_HDRS_MAX    192
#endif
#ifndef TIKU_BASIC_HTTP_REQ_MAX
#define TIKU_BASIC_HTTP_REQ_MAX     576
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
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_NAMEDVAR_MAX  64
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_NAMEDVAR_MAX  32
#  else
#    define TIKU_BASIC_NAMEDVAR_MAX  16
#  endif
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
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_STR_HEAP_BYTES 4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_HEAP_BYTES 2048
#  else
#    define TIKU_BASIC_STR_HEAP_BYTES 512
#  endif
#endif
#ifndef TIKU_BASIC_STR_BUF_CAP
#  if defined(TIKU_BASIC_TIER_BIG)
/* 1 KB on the big-RAM parts (Apollo/RP2350): each string-expression temporary
 * is a stack buffer of this size, and the stacks here are huge, so this lets
 * STRIP$()/HTTPGET$ clear the HTTP header block and show a chunk of the body
 * without risking deep-nesting overflow. BROWSE uses its own 16 KB buffer for
 * whole pages, so this only bounds the in-BASIC-string path. */
#    define TIKU_BASIC_STR_BUF_CAP  1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_BUF_CAP  128
#  else
#    define TIKU_BASIC_STR_BUF_CAP  64
#  endif
#endif

/* Big response buffers (referenced as #0, #1, ...): arena-backed, filled by the
 * FETCH statement, read in place by the extractors (JSON$/LINE$/BETWEEN$ with a
 * #n source, LEN(#n)). They hold a whole HTTP/LLM reply past the STR_BUF_CAP
 * scratch limit -- FETCH writes straight into the arena, bypassing the 1 KB
 * stack buffer. BIG-tier only (each buffer is real arena RAM). */
#ifndef TIKU_BASIC_BIGBUF_COUNT
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_BIGBUF_COUNT 2
#  else
#    define TIKU_BASIC_BIGBUF_COUNT 0
#  endif
#endif
#ifndef TIKU_BASIC_BIGBUF_SIZE
#  define TIKU_BASIC_BIGBUF_SIZE    8192
#endif

/* DEF FN single-line user functions. Each entry stores a name (up
 * to 7 chars), the argument-letter index, and an expression body
 * up to TIKU_BASIC_DEFN_BODY chars long. Calls evaluate the body
 * with the call-site argument bound to the named variable. */
#ifndef TIKU_BASIC_DEFN_ENABLE
#define TIKU_BASIC_DEFN_ENABLE      1
#endif
#ifndef TIKU_BASIC_DEFN_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_MAX     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_DEFN_MAX      8
#  else
#    define TIKU_BASIC_DEFN_MAX      4
#  endif
#endif
#ifndef TIKU_BASIC_DEFN_BODY
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_BODY    64
#  else
#    define TIKU_BASIC_DEFN_BODY    40
#  endif
#endif

/* DIM A(n) integer arrays. 26 slots (one per A..Z); each starts
 * unallocated. DIM bumps an arena allocation. Re-DIMming the same
 * variable in a single session is rejected. Arrays are zero-filled
 * at every RUN start to match basic_vars semantics. */
#ifndef TIKU_BASIC_ARRAYS_ENABLE
#define TIKU_BASIC_ARRAYS_ENABLE    1
#endif
/* ARRAY_MAX caps each DIM's per-dimension and total element count;
 * ARRAY_TOTAL_LONGS is the shared arena pool that backs every array's element
 * storage (must be >= one ARRAY_MAX array, more for several).  They scale
 * together.  ARRAY_TOTAL_LONGS is consumed in tiku_basic_arena.inl, which is
 * included after this header, so defining it here wins. */
#ifndef TIKU_BASIC_ARRAY_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_MAX    4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_MAX    1024
#  else
#    define TIKU_BASIC_ARRAY_MAX    128
#  endif
#endif
#ifndef TIKU_BASIC_ARRAY_TOTAL_LONGS
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 4096u
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 1024u
#  else
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 128u
#  endif
#endif

/* Multi-slot SAVE / LOAD. The default unnamed SAVE / LOAD continue
 * to use the original "prog" persist key; named slots live in
 * their own .persistent table. */
#ifndef TIKU_BASIC_NAMED_SLOTS
#define TIKU_BASIC_NAMED_SLOTS      3
#endif
/* Each named slot must hold a serialized program, so it scales with the
 * program size (these are static .persistent bytes, like the main save buf). */
#ifndef TIKU_BASIC_NAMED_SLOT_BYTES
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_NAMED_SLOT_BYTES 2048
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_NAMED_SLOT_BYTES 1024
#  else
#    define TIKU_BASIC_NAMED_SLOT_BYTES 192
#  endif
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
#elif defined(PLATFORM_AMBIQ)
/* On Ambiq the persist-backed save buffer is not built: the saved program is
 * durable in the carved NVM region (see basic_prog_store/fetch). This attribute
 * is unused there, kept defined for symmetry. */
#define BASIC_NVM_PERSISTENT __attribute__((section(".ssram")))
#else
#define BASIC_NVM_PERSISTENT
#endif

/* Transient SAVE/LOAD serialization scratch (a worst-case whole-program image:
 * ~90 KB at 1024 lines, ~176 KB at 2048). The two buffers are never live at the
 * same time, but together they would dominate the scarce DTCM (384-512 KB), so
 * on Ambiq they ride the multi-MB .ssram pool (zeroed at boot, like .bss); the
 * durable copy lives in the NVM region. Elsewhere the scratch stays in .bss. */
#if defined(PLATFORM_AMBIQ)
#define BASIC_SCRATCH __attribute__((section(".ssram")))
#else
#define BASIC_SCRATCH
#endif

#endif /* TIKU_BASIC_CONFIG_H_ */
