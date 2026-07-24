/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_program.inl - line table + accessors.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * `prog` is a flat array of basic_line_t with `number == 0`
 * marking empty slots.  prog_store inserts / replaces / deletes
 * lines; prog_next_index walks forward in numeric order;
 * prog_find_exact looks up by line number; prog_list streams the
 * program through the shell I/O (used by the LIST command).  All
 * callers go through these helpers, so the array layout is
 * encapsulated.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The prog_* helpers below scan prog[] with a uint16_t loop counter, so the
 * configured line-table size must fit in 16 bits.  A narrower (uint8_t)
 * counter silently looped forever once the per-tier PROGRAM_LINES limits were
 * raised above 255 (1024 on Apollo4 Lite/RP2350, 2048 on Apollo510, 256 on
 * FRAM): `i < TIKU_BASIC_PROGRAM_LINES` stayed perpetually true and prog_store
 * spun on the first stored line. */
_Static_assert(TIKU_BASIC_PROGRAM_LINES <= 0xFFFFu,
               "PROGRAM_LINES must fit the uint16_t prog[] scan counter");

/* A3: any edit invalidates the derived line-number index AND the SUB/label
 * registry (both are rebuilt on their next lookup). */
#define PROG_INDEX_INVALIDATE()  (basic_line_index_ok = 0, basic_symreg_ok = 0)

/** @brief Mark every line slot empty. */
static void
prog_clear(void)
{
    uint16_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) prog[i].number = 0;
    PROG_INDEX_INVALIDATE();
}

static int
prog_store(uint16_t lineno, const char *body)
{
    uint16_t i;
    char        crn[TIKU_BASIC_LINE_MAX];
    const char *t = body;
    skip_ws(&t);
    PROG_INDEX_INVALIDATE();
    /* Empty body -> delete the line if present. */
    if (*t == '\0') {
        for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
            if (prog[i].number == lineno) prog[i].number = 0;
        }
        return 0;
    }
    /* A2: crunch keywords to token bytes at store time.  Output is never
     * longer than the input, and LIST / SAVE detokenize, so the on-media and
     * on-screen forms stay plain text. */
    basic_crunch(crn, sizeof(crn), t);
    t = crn;
    /* Replace existing line. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) {
            strncpy(prog[i].text, t, TIKU_BASIC_LINE_MAX - 1);
            prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
            return 0;
        }
    }
    /* Find empty slot. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0) {
            prog[i].number = lineno;
            strncpy(prog[i].text, t, TIKU_BASIC_LINE_MAX - 1);
            prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

/*--- A3: linear fallbacks (used when the index isn't ready + to build it) ---*/

static int
prog_next_index_linear(uint16_t lineno)
{
    int      best     = -1;
    uint16_t best_num = 0xFFFF;
    uint16_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0)        continue;
        if (prog[i].number < lineno)    continue;
        if (prog[i].number < best_num) {
            best_num = prog[i].number;
            best = (int)i;
        }
    }
    return best;
}

static int
prog_find_exact_linear(uint16_t lineno)
{
    uint16_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) return (int)i;
    }
    return -1;
}

/* Build basic_line_order[] = active prog[] indices, ascending by line number.
 * Collect in physical order (== insertion order, usually already ascending)
 * then insertion-sort -- O(N) on a program entered in order, O(N^2) worst case
 * on a reverse-entered one, paid once per edit and amortized over the run. */
static void
basic_line_index_build(void)
{
    uint16_t i, n = 0;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number != 0) basic_line_order[n++] = i;
    }
    for (i = 1; i < n; i++) {
        uint16_t key  = basic_line_order[i];
        uint16_t knum = prog[key].number;
        int      j    = (int)i - 1;
        while (j >= 0 && prog[basic_line_order[j]].number > knum) {
            basic_line_order[j + 1] = basic_line_order[j];
            j--;
        }
        basic_line_order[j + 1] = key;
    }
    basic_line_count    = n;
    basic_line_index_ok = 1;
}

/* Index of the lowest-numbered line whose number >= @p lineno; -1 if none.
 * O(log N) lower-bound over the sorted index (identical result to the linear
 * scan, which it falls back to before the arena / index is ready). */
static int
prog_next_index(uint16_t lineno)
{
    uint16_t lo, hi;
    if (basic_line_order == NULL) return prog_next_index_linear(lineno);
    if (!basic_line_index_ok)     basic_line_index_build();
    lo = 0; hi = basic_line_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
        if (prog[basic_line_order[mid]].number < lineno) lo = (uint16_t)(mid + 1);
        else                                             hi = mid;
    }
    return (lo < basic_line_count) ? (int)basic_line_order[lo] : -1;
}

static int
prog_find_exact(uint16_t lineno)
{
    uint16_t lo, hi;
    /* Line 0 marks an empty slot, never a real line -- preserve the linear
     * scan's exact-0 behaviour (returns the first empty slot) for any caller
     * that relies on it; the index holds only active lines. */
    if (lineno == 0)              return prog_find_exact_linear(0);
    if (basic_line_order == NULL) return prog_find_exact_linear(lineno);
    if (!basic_line_index_ok)     basic_line_index_build();
    lo = 0; hi = basic_line_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
        uint16_t num = prog[basic_line_order[mid]].number;
        if      (num < lineno) lo = (uint16_t)(mid + 1);
        else if (num > lineno) hi = mid;
        else                   return (int)basic_line_order[mid];
    }
    return -1;
}

static void
prog_list(void)
{
    uint16_t cur = 0;
    while (1) {
        int idx = prog_next_index(cur);
        if (idx < 0) break;
        SHELL_PRINTF("%u ", (unsigned)prog[idx].number);
        basic_detok_print(prog[idx].text);   /* A2: expand token bytes */
        SHELL_PRINTF("\n");
        if (prog[idx].number == 0xFFFFu) break;
        cur = (uint16_t)(prog[idx].number + 1);
    }
}
