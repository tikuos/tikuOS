/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_program.inl - the line table and its accessors.
 *
 * A flat array of lines, number 0 marking an empty slot, with helpers to store,
 * walk in line order, look up and list.  Ordered walks and exact lookups
 * binary-search a sorted index that every edit invalidates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The prog_* helpers scan prog[] with a uint16_t loop counter, so the
 * configured line-table size must fit in 16 bits. */
_Static_assert(TIKU_BASIC_PROGRAM_LINES <= 0xFFFFu,
               "PROGRAM_LINES must fit the uint16_t prog[] scan counter");

/**
 * @brief Mark the line index and the SUB/label registry stale after an edit;
 *        both are rebuilt on their next lookup.
 */
#define PROG_INDEX_INVALIDATE()  (basic_line_index_ok = 0, basic_symreg_ok = 0)

/** @brief Mark every line slot empty. */
static void
prog_clear(void)
{
    uint16_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) prog[i].number = 0;
    PROG_INDEX_INVALIDATE();
}

/**
 * @brief Store line @p lineno, replacing any line of that number; an empty
 *        @p body deletes it.
 * @return 0, or -1 when the table is full
 */
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
    /* Crunch keywords to token bytes at store time.  Output is never longer
     * than the input, and LIST / SAVE detokenize, so the on-media and
     * on-screen forms stay plain text. */
    basic_crunch(crn, sizeof(crn), t);
    t = crn;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) {
            strncpy(prog[i].text, t, TIKU_BASIC_LINE_MAX - 1);
            prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
            return 0;
        }
    }
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

/** @brief prog_next_index() by linear scan, for when no index is allocated. */
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

/** @brief prog_find_exact() by linear scan. */
static int
prog_find_exact_linear(uint16_t lineno)
{
    uint16_t i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == lineno) return (int)i;
    }
    return -1;
}

/**
 * @brief Rebuild basic_line_order[]: active prog[] indices by line number.
 *
 * Collects in slot order, then insertion-sorts: O(N) for a program entered in
 * order, O(N^2) for one entered in reverse, paid once per edit.
 */
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

/**
 * @brief Slot of the lowest-numbered line whose number is >= @p lineno.
 *
 * A lower-bound search over the sorted index; before the arena holds an index
 * it falls back to the linear scan, with the same result.
 *
 * @return The prog[] index, or -1 when no such line exists
 */
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

/**
 * @brief Slot of line @p lineno, by binary search over the sorted index.
 * @return The prog[] index (for 0, the first empty slot), or -1 when absent
 */
static int
prog_find_exact(uint16_t lineno)
{
    uint16_t lo, hi;
    /* Line 0 marks an empty slot, never a real line, and the index holds
     * only active lines, so 0 takes the linear scan and finds the first
     * empty slot. */
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

/** @brief Print the program in line order with keywords expanded (LIST). */
static void
prog_list(void)
{
    uint16_t cur = 0;
    while (1) {
        int idx = prog_next_index(cur);
        if (idx < 0) break;
        SHELL_PRINTF("%u ", (unsigned)prog[idx].number);
        basic_detok_print(prog[idx].text);   /* expand token bytes */
        SHELL_PRINTF("\n");
        if (prog[idx].number == 0xFFFFu) break;
        cur = (uint16_t)(prog[idx].number + 1);
    }
}
