/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_state.inl - core types and module-level state.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Defines the line / FOR-frame / loop-frame / DEF-FN / array
 * structs, the arena-backed pointers (`prog`, `basic_vars`, ...),
 * the FRAM-backed persistent buffers, and all the interpreter
 * status flags (basic_running, basic_pc, basic_error, AUTO state,
 * ON ERROR handler, EVERY / ON CHANGE registries, TRACE, DATA
 * cursor).
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
/* PROGRAM LINE                                                              */
/*---------------------------------------------------------------------------*/

typedef struct {
    uint16_t number;                            /* 0 == empty slot */
    char     text[TIKU_BASIC_LINE_MAX];
} basic_line_t;

/*---------------------------------------------------------------------------*/
/* CONTROL-FLOW FRAMES                                                       */
/*---------------------------------------------------------------------------*/

/* FOR-loop frame. The loop body is the range of program lines
 * starting at `loop_line` (the line AFTER the FOR statement); on
 * NEXT we step the index var, compare against `target`, and either
 * jump back to `loop_line` or pop. */
typedef struct {
    uint8_t  var_idx;       /* 0..25, the loop variable */
    long     target;        /* TO value */
    long     step;          /* STEP value (default 1) */
    uint16_t loop_line;     /* line to jump back to on continuation */
} basic_for_frame_t;

/*---------------------------------------------------------------------------*/
/* ARENA-BACKED STATE                                                        */
/*---------------------------------------------------------------------------*/

/* All program / state storage lives in a kernel arena rather than
 * BSS. The arena is allocated lazily from the AUTO tier on the first
 * `basic` invocation, sized to hold the line table, the variable
 * table, and the GOSUB stack with alignment slack. Subsequent
 * invocations reset the arena and re-allocate, achieving the
 * fresh-state-on-entry behavior without permanent BSS cost.
 *
 * Memory cost: ~50 bytes BSS (arena control block + four pointers
 * + a ready flag), instead of ~1.3 KB of dedicated arrays. The
 * arena's actual storage comes from the kernel's tier backing pool,
 * which is shared infrastructure -- if `basic` is never invoked,
 * the bytes stay free for other features. */
static tiku_arena_t  basic_arena;
static int           basic_arena_ready;
static basic_line_t *prog;
static long         *basic_vars;
static uint16_t     *gosub_stack;
static uint8_t       gosub_sp;
static basic_for_frame_t *for_stack;
static uint8_t       for_sp;

/* Unified WHILE/REPEAT loop-frame stack. Each frame remembers the
 * line number to jump back to: the WHILE line itself (so WHILE re-
 * evaluates its condition) or the line after REPEAT (so the body
 * re-runs). */
typedef struct {
    uint16_t back_line;
} basic_loop_frame_t;
static basic_loop_frame_t *loop_stack;
static uint8_t       loop_sp;

#if TIKU_BASIC_STRVARS_ENABLE
/* String variables A$..Z$ and the bump-allocated heap they point
 * into. basic_strvars[i] is either NULL (unbound) or points into
 * basic_str_heap. The heap and the var table both live inside the
 * BASIC arena -- see basic_alloc_state(). */
static char        **basic_strvars;
static char         *basic_str_heap;
static uint16_t      basic_str_heap_pos;
#endif

#if TIKU_BASIC_DEFN_ENABLE
#ifndef TIKU_BASIC_DEFN_ARGS
#define TIKU_BASIC_DEFN_ARGS 4
#endif
typedef struct {
    char    name[8];                                   /* "" = empty */
    uint8_t arg_count;                                 /* 0..TIKU_BASIC_DEFN_ARGS */
    uint8_t arg_idx[TIKU_BASIC_DEFN_ARGS];             /* var indices (0..25) */
    char    body[TIKU_BASIC_DEFN_BODY];
} basic_defn_t;
static basic_defn_t *basic_defns;
#endif

#if TIKU_BASIC_ARRAYS_ENABLE
/* 1D / 2D integer or string arrays. Stored row-major as a flat
 * buffer (long[] for numeric, char*[] for string). For 1D arrays
 * dim2 == 0 and we treat (i) as element [i]; for 2D, (i, j) is
 * element [i * dim2 + j]. The numeric and string array tables are
 * separate so A and A$ can both be DIMmed independently. */
typedef struct basic_array_s {
    void    *data;          /* NULL until DIMmed; long[] or char*[] */
    uint16_t dim1;          /* outer dimension (always set) */
    uint16_t dim2;          /* 0 for 1D, inner dim for 2D */
    uint8_t  is_string;     /* 1 -> data is char*[]  */
} basic_array_t;
static basic_array_t *basic_arrays;       /* numeric: A..Z */
#if TIKU_BASIC_STRVARS_ENABLE
static basic_array_t *basic_str_arrays;   /* string : A$..Z$ */
#endif

/* Forward decl so parse_strprim (defined above the implementation
 * of parse_array_index) can call it. */
static long parse_array_index(const char **p,
                              basic_array_t *slot, char letter);
#endif

/*---------------------------------------------------------------------------*/
/* FRAM-BACKED PERSISTENT STATE                                              */
/*---------------------------------------------------------------------------*/

/* FRAM-backed persistent state. The saved-program buffer + the
 * persist-store metadata both live in the .persistent section so
 * they survive power cycles. tiku_persist_init() validates entries
 * via the magic number on every boot. */
static BASIC_NVM_PERSISTENT uint8_t basic_save_buf[TIKU_BASIC_SAVE_BUF_BYTES];
static BASIC_NVM_PERSISTENT tiku_persist_store_t basic_store;
static uint8_t       basic_persist_ready;

/*---------------------------------------------------------------------------*/
/* INTERPRETER STATUS FLAGS                                                  */
/*---------------------------------------------------------------------------*/

/* PRNG for RND(). Seeded lazily on first call from
 * tiku_clock_time(). Linear-congruential -- cheap on MSP430 and
 * has good enough properties for casual BASIC games / test data. */
static uint32_t      basic_prng_state;
static uint8_t       basic_prng_seeded;

static int          basic_running;     /* 1 while RUN loop active */
static uint16_t     basic_pc;
static int          basic_pc_set;      /* 1 if exec_stmt explicitly set PC */
static int          basic_error;
static int          basic_quit;        /* 1 when BYE is typed */

/* AUTO line-numbering at the REPL: when active, the REPL prompt
 * prepends `next` and increments by `step` after each line. Disable
 * with `AUTO OFF` or by typing a blank line. */
static uint16_t     basic_auto_next;
static uint16_t     basic_auto_step;
static int          basic_auto_active;

/* ON ERROR GOTO N: when an error fires during RUN, jump to N
 * instead of aborting. 0 = handler disabled (default behaviour).
 * `basic_err_pc` records the line that errored, so RESUME and
 * RESUME NEXT know where to continue from. */
static uint16_t     basic_err_handler;
static uint16_t     basic_err_pc;

/* EVERY ms : stmt -- recurring scheduled statement. Polled by the
 * RUN loop between program lines. Up to TIKU_BASIC_EVERY_MAX active
 * registrations; the entire table resets at every RUN start so a
 * fresh program doesn't inherit stale handlers. */
#ifndef TIKU_BASIC_EVERY_MAX
#define TIKU_BASIC_EVERY_MAX        4
#endif
#ifndef TIKU_BASIC_EVERY_STMT_LEN
#define TIKU_BASIC_EVERY_STMT_LEN   32
#endif
typedef struct {
    long  interval_ms;
    long  next_due_ms;
    char  stmt[TIKU_BASIC_EVERY_STMT_LEN];
    uint8_t active;
} basic_every_t;
static basic_every_t *basic_everys;

/* ON CHANGE "/path" GO[SUB] line -- reactive VFS-watch handler.
 * The RUN loop polls each registration between program lines:
 * VFSREAD the path, compare to last value, fire handler on change. */
#ifndef TIKU_BASIC_ONCHG_MAX
#define TIKU_BASIC_ONCHG_MAX        4
#endif
typedef struct {
    char     path[40];
    long     last_value;
    uint16_t handler_line;
    uint8_t  is_gosub;
    uint8_t  active;
} basic_onchg_t;
static basic_onchg_t *basic_onchgs;

/* TRACE ON/OFF -- when set, RUN echoes each line before executing it.
 * Persists across BASIC sessions intentionally (it's a debug aid; the
 * user toggles it back off when done). */
static int          basic_trace;

/* READ / DATA / RESTORE state. The DATA pointer is (line-index,
 * byte-offset within prog[idx].text). -1 line index means "find the
 * first DATA line". RESTORE resets to -1. */
static int          basic_data_idx;    /* index into prog[] of the current DATA line, -1 = none yet */
static int          basic_data_off;    /* byte offset within prog[basic_data_idx].text */
