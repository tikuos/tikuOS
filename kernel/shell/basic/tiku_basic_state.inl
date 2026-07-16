/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
    uint16_t var_idx;       /* index into basic_vars[] (0..) */
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
/* Arena offset just past the fixed working-set allocations (the line table,
 * variable tables, stacks, string heap, and array descriptor tables). Array
 * *element* storage is the only thing bump-allocated from the arena after this
 * point -- lazily, on DIM -- so rewinding the arena to this mark reclaims
 * exactly that storage without disturbing the fixed set: the array analogue of
 * the per-RUN basic_str_heap_pos reset. Captured at the end of
 * basic_alloc_state(); used by basic_clear_vars(). */
static tiku_mem_arch_size_t basic_arena_mark;
static basic_line_t *prog;

/* A3: derived line-number index -- prog[] indices sorted ascending by line
 * number, so prog_find_exact / prog_next_index binary-search instead of
 * linear-scanning the whole table on every executed line.  prog[] itself is
 * left UNSORTED (no change to prog_store / RENUM / the empty-slot invariant);
 * the index is demand-rebuilt after any edit.  Behaviour is identical to the
 * old linear scans -- purely a speedup. */
static uint16_t     *basic_line_order;    /* [basic_line_count] valid entries  */
static uint16_t      basic_line_count;    /* number of active lines            */
static int           basic_line_index_ok; /* 1 = index reflects current prog[] */

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
#if TIKU_BASIC_BIGBUF_COUNT > 0
/* Big response buffers (#0..): arena-backed, filled by FETCH, read in place by
 * the extractors. basic_biglen[n] is the current byte length (0 = empty). */
static char         *basic_bigbuf[TIKU_BASIC_BIGBUF_COUNT];
static size_t        basic_biglen[TIKU_BASIC_BIGBUF_COUNT];
#endif
#endif

/* Multi-letter variable names: index space [26, 26+N) backed by
 * the same basic_vars / basic_strvars arrays as A..Z, with parallel
 * name tables that hold the identifier text per slot.  An empty
 * name[0] == '\0' marks an unallocated slot.  Names are stored
 * upper-case (case is normalised at parse time). */
static char (*basic_namedvar_names)[TIKU_BASIC_NAMEDVAR_LEN];
#if TIKU_BASIC_STRVARS_ENABLE
static char (*basic_namedstrvar_names)[TIKU_BASIC_NAMEDVAR_LEN];
#endif

/* CONST NAME = expr (F4): 1 marks the numeric named-var slot (index = slot -
 * 26) as read-only.  Reset each RUN by basic_clear_vars.  Not serialized by
 * the F1 checkpoint, so a power-cut RESUME loses read-only enforcement until
 * the CONST line re-executes -- same boundary as arrays/EVERY. */
static uint8_t basic_namedvar_const[TIKU_BASIC_NAMEDVAR_MAX];

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

/* SUB call frames + the LOCAL restore stack.  Declared here -- ahead of
 * tiku_basic_ckpt.inl -- so the F1 checkpoint can serialize them; the SUB/CALL
 * logic that drives them lives in tiku_basic_subs.inl (included later). */
#if TIKU_BASIC_SUBS_ENABLE
#ifndef TIKU_BASIC_CALL_DEPTH
#define TIKU_BASIC_CALL_DEPTH  8
#endif
#ifndef TIKU_BASIC_SCOPE_MAX
#define TIKU_BASIC_SCOPE_MAX   32      /* total saved params+locals, all frames */
#endif
/* One saved variable slot for a SUB param / LOCAL.  is_str selects which
 * array the slot belongs to: numeric slots restore `old` into basic_vars,
 * string slots restore `old_str` into basic_strvars.  old_str points INTO
 * basic_str_heap, so it is (a) an A4 compaction root -- a caller's shadowed
 * string is reachable only through here -- and (b) serialized as a heap
 * offset by the F1 checkpoint (a raw pointer would not survive a power cut). */
typedef struct {
    uint16_t idx;
    uint8_t  is_str;
    long     old;         /* numeric saved value   (is_str == 0) */
    char    *old_str;     /* string saved pointer   (is_str == 1), else NULL */
} basic_scope_t;
typedef struct { uint16_t ret_line; uint8_t scope_base; } basic_frame_t;
static basic_scope_t basic_scope[TIKU_BASIC_SCOPE_MAX];
static uint8_t       basic_scope_sp;
static basic_frame_t basic_frames[TIKU_BASIC_CALL_DEPTH];
static uint8_t       basic_call_sp;

/* SUB return value (F3): a SUB sets it with the `RESULT expr` statement; the
 * caller reads it as the bare `RESULT` numeric function after CALL.  Gives a
 * scoped return without leaking through a global.  Reset each RUN. */
static long          basic_sub_result;
#endif

#if TIKU_BASIC_EXT_MAX > 0
/* Native builtin registry (tiku_basic_ext.h, Tier 2 of loadable.md).
 * Boot-registered, deliberately OUTSIDE the arena and the F1 checkpoint --
 * it is firmware configuration, not program state.  Names are uppercase and
 * never in the A2 token table, so stored (crunched) lines reach them through
 * match_kw's raw-text path at the dispatch fallthroughs. */
typedef struct {
    char    name[TIKU_BASIC_EXT_NAME_MAX];   /* "" = free slot */
    uint8_t kind;                            /* 0 = statement, 1 = numeric fn */
    uint8_t arity;                           /* numeric fns: 0..2 */
    union {
        tiku_basic_ext_stmt_fn stmt;
        tiku_basic_ext_nfn     nfn;
    } u;
} basic_ext_entry_t;
static basic_ext_entry_t basic_ext_tab[TIKU_BASIC_EXT_MAX];
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

/* Durable saved-program state, buffer-backed variant.  The save buffer + the
 * persist-store metadata carry BASIC_NVM_PERSISTENT (.persistent FRAM on
 * MSP430 -- durable; plain .bss on Nordic/host -- session-only);
 * tiku_persist_init() validates entries via the magic number on every boot.
 * Not built on the region-backed parts (Ambiq MRAM, RP2350 flash): there the
 * saved program lives at a fixed offset in the carved NVM region's reserved
 * tail instead -- see basic_prog_store/fetch in tiku_basic_persist.inl. */
#if !BASIC_NVM_ON_REGION
static BASIC_NVM_PERSISTENT uint8_t basic_save_buf[TIKU_BASIC_SAVE_BUF_BYTES];
static BASIC_NVM_PERSISTENT tiku_persist_store_t basic_store;
static uint8_t       basic_persist_ready;
#endif

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

/* Shell-mode state (tiku_basic_mode.inl).  BASIC is a non-blocking MODE of the
 * shell process rather than a blocking takeover: basic_mode_on is 1 while the
 * shell is in BASIC mode, basic_mode_interactive distinguishes the REPL
 * (`basic`, shows a prompt) from a headless run (`basic run`, no prompt).
 * Declared here (early) so process_line's RUN handler can test basic_mode_on;
 * the mode functions themselves live in tiku_basic_mode.inl. */
static uint8_t      basic_mode_on;
static uint8_t      basic_mode_interactive;

/* F1 checkpoint arming (tiku_basic_ckpt.inl).  1 while PERSIST is ON: the run
 * loop checkpoints the reified execution state at each yield boundary so RUN
 * RESUME can continue mid-loop across a reset / power cut.  Declared here (with
 * the other run-scope flags) so the dispatcher, the run loop, and the mode
 * driver can all see it; the checkpoint engine itself is in tiku_basic_ckpt.inl.
 * A per-boot SRAM flag -- reset on power-up, then re-armed by RUN RESUME (which
 * restores it from the checkpoint) or a fresh PERSIST ON. */
static uint8_t      basic_ckpt_armed;

/* AUTO line-numbering at the REPL: when active, the REPL prompt
 * prepends `next` and increments by `step` after each line. Disable
 * with `AUTO OFF` or by typing a blank line. */
static uint16_t     basic_auto_next;
static uint16_t     basic_auto_step;
static int          basic_auto_active;

/* Yielding wait (mode DELAY / SLEEP).  A blocking in-statement spin starves
 * the entire shell event loop for its duration -- rules, watch, and BASIC's
 * own event-armed ON CHANGE all stall (found in the LM20 F2 HITL).  In shell
 * mode, exec_delay/exec_sleep instead record a deadline here and PARK the
 * step machine: basic_run_step returns RUNNING without executing until the
 * deadline passes, then resumes the interrupted line at basic_wait_off.
 * Semantics match the blocking wait exactly (the line's remaining
 * statements run after the pause; reactive polls stay suppressed during it)
 * -- but the shell loop breathes, so queued events dispatch and pending
 * ON CHANGE marks fire at the first post-wait statement boundary.
 * basic_wait_sleep_s chunks long SLEEPs below the tick counter's wrap.
 * Only the MAIN line walker yields (basic_stmt_depth == 1): DELAY inside an
 * IF-THEN scratch or an EVERY body keeps the old blocking behaviour, since
 * their transient buffers cannot be resumed across ticks. */
/* 1 while the RUN loop is driven as a non-blocking shell mode
 * (tiku_basic_mode_*), 0 for the synchronous exec_run path.  When set,
 * basic_run_step() skips its inline Ctrl-C poll (the shell loop routes
 * keystrokes) and DELAY/SLEEP park instead of spinning. */
static uint8_t           basic_run_shell_mode;

static uint8_t           basic_wait_pending;
static tiku_clock_time_t basic_wait_start;
static tiku_clock_time_t basic_wait_ticks;
static uint16_t          basic_wait_line;    /* line to resume             */
static uint16_t          basic_wait_off;     /* byte offset into its text  */
static long              basic_wait_sleep_s; /* SLEEP: remaining seconds   */
static uint8_t           basic_stmt_depth;   /* exec_stmts nesting         */
static uint8_t           basic_in_reactive;  /* inside basic_poll_reactive */

/* ON ERROR GOTO N: when an error fires during RUN, jump to N
 * instead of aborting. 0 = handler disabled (default behaviour).
 * `basic_err_pc` records the line that errored, so RESUME and
 * RESUME NEXT know where to continue from. */
static uint16_t     basic_err_handler;
static uint16_t     basic_err_pc;

/* ERR / ERL introspection for ON ERROR handlers.  `basic_errcat` is a
 * transient per-statement category hint: throw sites that can classify
 * (range, divide-by-zero, net, I/O, ...) set it, and it is cleared
 * before each statement.  When an error is trapped it is frozen into
 * the sticky `basic_err` (returned by ERR) together with the erroring
 * line in `basic_erl` (returned by ERL), so both survive into the
 * handler.  Uncategorised errors surface as TIKU_BASIC_ERR_GENERAL. */
static int          basic_errcat;
static int          basic_err;
static uint16_t     basic_erl;

/*---------------------------------------------------------------------------*/
/* CENTRALIZED ERROR THROW (A5)                                              */
/*                                                                           */
/* Every interpreter error funnels through basic_throw() / basic_throwf():   */
/* set basic_error + basic_errcat, then emit the message through a swappable  */
/* sink.  The default sink is the shell console (red "? msg").  An embedder   */
/* installs its own via tiku_basic_set_error_sink() to capture errors into a  */
/* buffer and run BASIC completely HEADLESS -- no shell/UART required.  This  */
/* is the single seam the agent/library direction needs; it also makes ERR /  */
/* ERL categories consistent, since the category is now supplied at the one   */
/* throw call instead of being set (or forgotten) ad hoc per site.           */
/*---------------------------------------------------------------------------*/

/* Sink (tiku_basic_error_sink_t, declared in tiku_basic.h) receives the
 * category + the bare message (no color, no "? " prefix, no newline).
 * NULL => the default console sink below. */
static tiku_basic_error_sink_t basic_error_sink;

void
tiku_basic_set_error_sink(tiku_basic_error_sink_t sink)
{
    basic_error_sink = sink;
}

static void
basic_emit_error(int cat, const char *msg)
{
    if (basic_error_sink != NULL) {
        basic_error_sink(cat, msg);
    } else {
        SHELL_PRINTF(SH_RED "? %s\n" SH_RST, msg);
    }
    (void)cat;
}

/* Throw with a fixed message. */
static void
basic_throw(int cat, const char *msg)
{
    basic_error  = 1;
    basic_errcat = cat;
    basic_emit_error(cat, msg);
}

/* Throw with a printf-style message (for the handful of sites that splice in
 * a name/path/number).  Formats into a transient buffer so the sink still
 * sees fully-rendered text in headless mode. */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void
basic_throwf(int cat, const char *fmt, ...)
{
    char    buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    basic_throw(cat, buf);
}

/* Emit a message through the same sink WITHOUT flagging a runtime error --
 * for REPL / command-level notices (bad line number, save failed, "no
 * program", ...) that are not interpreter throws but must still honor a
 * headless sink so nothing writes red to the console behind its back.  The
 * invariant this preserves: basic_emit_error is the ONLY console error writer. */
static void
basic_report(int cat, const char *msg)
{
    basic_emit_error(cat, msg);
}

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
static void
basic_reportf(int cat, const char *fmt, ...)
{
    char    buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    basic_emit_error(cat, buf);
}

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

/* F2: make ON CHANGE on WRITABLE nodes event-driven via tiku_vfs_watch instead
 * of polling VFSREAD every tick -- exact (no missed fast transitions) and
 * cheaper.  Needs the real kernel VFS watch API + the shell process's event
 * queue, so it is ON for the real platforms and OFF on the host test harness
 * (which has neither).  -D-overridable. */
#ifndef TIKU_BASIC_ONCHG_EVENT
#  if TIKU_BASIC_ONCHG_MAX > 0 && (defined(PLATFORM_MSP430) ||                 \
       defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) ||                  \
       defined(PLATFORM_NORDIC))
#    define TIKU_BASIC_ONCHG_EVENT   1
#  else
#    define TIKU_BASIC_ONCHG_EVENT   0
#  endif
#endif

typedef struct {
    char     path[40];
    long     last_value;
    uint16_t handler_line;
    uint8_t  is_gosub;
    uint8_t  active;
#if TIKU_BASIC_ONCHG_EVENT
    /* Cached resolved node.  Non-NULL with a write handler => event-armed
     * (watched; the poll tick only re-checks it when an event marks it
     * pending).  NULL / read-only => polled every tick as before. */
    const tiku_vfs_node_t *node;
    uint8_t                armed;      /* 1 = subscribed via tiku_vfs_watch  */
    uint8_t                pending;    /* 1 = an event arrived, re-check due  */
#endif
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
