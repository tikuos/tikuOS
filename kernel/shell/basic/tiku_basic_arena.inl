/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_arena.inl - arena allocation for the BASIC working set.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Computes the arena footprint from the configured limits, then
 * lazily allocates the line table, variable table, GOSUB / FOR /
 * loop stacks, EVERY / ON CHANGE registries, string heap, DEF FN
 * table, and array tables out of the AUTO memory tier.  Subsequent
 * BASIC sessions reset the arena and re-allocate, so the cost is
 * "fresh state per entry" without paying permanent BSS for the full
 * feature set.
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
/* ARENA SIZING                                                              */
/*---------------------------------------------------------------------------*/

/* Total var-table width = 26 single-letter A..Z slots + named pool.
 * Used when sizing the arena and when walking var arrays. */
#define BASIC_VAR_TABLE_LEN  (26u + (unsigned)TIKU_BASIC_NAMEDVAR_MAX)

/* Compute the arena size we need for the configured limits, with a
 * small slack for word alignment between sub-allocations. */
#if TIKU_BASIC_STRVARS_ENABLE
#define BASIC_ARENA_STR_BYTES                                               \
    (sizeof(char *) * BASIC_VAR_TABLE_LEN +                                 \
     (size_t)TIKU_BASIC_STR_HEAP_BYTES +                                    \
     TIKU_BASIC_NAMEDVAR_LEN * (size_t)TIKU_BASIC_NAMEDVAR_MAX)
#else
#define BASIC_ARENA_STR_BYTES 0u
#endif

#define BASIC_ARENA_NAMEDVAR_BYTES \
    (TIKU_BASIC_NAMEDVAR_LEN * (size_t)TIKU_BASIC_NAMEDVAR_MAX)
#if TIKU_BASIC_DEFN_ENABLE
#define BASIC_ARENA_DEFN_BYTES \
    (sizeof(basic_defn_t) * TIKU_BASIC_DEFN_MAX)
#else
#define BASIC_ARENA_DEFN_BYTES 0u
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
#if TIKU_BASIC_STRVARS_ENABLE
#define BASIC_ARENA_ARRAYS_BYTES \
    (sizeof(basic_array_t) * 26u * 2u)         /* numeric + string */
#else
#define BASIC_ARENA_ARRAYS_BYTES \
    (sizeof(basic_array_t) * 26u)
#endif
/* Array element storage allocates from the arena lazily on DIM, so
 * we just reserve enough total headroom for one or two reasonably-
 * sized arrays; the exact cap depends on what else has been
 * allocated by the time DIM is invoked. Override via
 * TIKU_BASIC_ARRAY_TOTAL_LONGS to bump it. */
#ifndef TIKU_BASIC_ARRAY_TOTAL_LONGS
#define TIKU_BASIC_ARRAY_TOTAL_LONGS 128u
#endif
#define BASIC_ARENA_ARRAY_DATA_BYTES \
    ((size_t)TIKU_BASIC_ARRAY_TOTAL_LONGS * sizeof(long))
#else
#define BASIC_ARENA_ARRAYS_BYTES     0u
#define BASIC_ARENA_ARRAY_DATA_BYTES 0u
#endif

#if TIKU_BASIC_BIGBUF_COUNT > 0
#define BASIC_ARENA_BIGBUF_BYTES \
    ((size_t)TIKU_BASIC_BIGBUF_COUNT * (size_t)TIKU_BASIC_BIGBUF_SIZE)
#else
#define BASIC_ARENA_BIGBUF_BYTES 0u
#endif

#define BASIC_ARENA_BYTES                                                   \
    ((tiku_mem_arch_size_t)(                                                \
        sizeof(basic_line_t)       * TIKU_BASIC_PROGRAM_LINES +             \
        sizeof(long)               * BASIC_VAR_TABLE_LEN +                  \
        sizeof(uint16_t)           * TIKU_BASIC_GOSUB_DEPTH +               \
        sizeof(basic_for_frame_t)  * TIKU_BASIC_FOR_DEPTH +                 \
        sizeof(basic_loop_frame_t) * TIKU_BASIC_LOOP_DEPTH +                \
        sizeof(basic_every_t)      * TIKU_BASIC_EVERY_MAX +                 \
        sizeof(basic_onchg_t)      * TIKU_BASIC_ONCHG_MAX +                 \
        BASIC_ARENA_NAMEDVAR_BYTES +                                        \
        BASIC_ARENA_STR_BYTES +                                             \
        BASIC_ARENA_DEFN_BYTES +                                            \
        BASIC_ARENA_ARRAYS_BYTES +                                          \
        BASIC_ARENA_ARRAY_DATA_BYTES +                                      \
        BASIC_ARENA_BIGBUF_BYTES +                                          \
        128u))   /* alignment headroom */

/*---------------------------------------------------------------------------*/
/* ALLOCATION                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset the BASIC variable namespace to its just-entered state.
 *
 * Clears every user-visible binding -- scalar vars (A..Z + the named pool),
 * string vars and their heap, numeric and string arrays, and DEF FN
 * definitions -- and rewinds the arena to basic_arena_mark so the element
 * storage of any DIMmed arrays is reclaimed (the array counterpart of the
 * per-RUN string-heap reset). The program line table is deliberately left
 * untouched: RUN clears the variables but keeps the program, while NEW / LOAD
 * clear the program separately via prog_clear().
 *
 * Shared by basic_alloc_state() (initial entry), NEW, RUN, and LOAD so all
 * four agree on exactly what "fresh variables" means -- and so re-DIMming an
 * array across runs no longer trips "array already DIMmed".
 */
static void
basic_clear_vars(void)
{
    uint16_t i;

    /* Reclaim DIMmed array element storage: it is the only thing allocated
     * from the arena past the mark, so this rewind frees all of it at once. */
    basic_arena.offset = basic_arena_mark;

    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) basic_vars[i] = 0;
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        basic_namedvar_names[i][0] = '\0';
    }
#if TIKU_BASIC_STRVARS_ENABLE
    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) basic_strvars[i] = NULL;
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        basic_namedstrvar_names[i][0] = '\0';
    }
    basic_str_heap_pos = 0;
#if TIKU_BASIC_BIGBUF_COUNT > 0
    for (i = 0; i < TIKU_BASIC_BIGBUF_COUNT; i++) basic_biglen[i] = 0;
#endif
#endif
#if TIKU_BASIC_DEFN_ENABLE
    for (i = 0; i < TIKU_BASIC_DEFN_MAX; i++) basic_defns[i].name[0] = '\0';
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
    for (i = 0; i < 26u; i++) {
        basic_arrays[i].data = NULL;
        basic_arrays[i].dim1 = 0;
        basic_arrays[i].dim2 = 0;
        basic_arrays[i].is_string = 0;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    for (i = 0; i < 26u; i++) {
        basic_str_arrays[i].data = NULL;
        basic_str_arrays[i].dim1 = 0;
        basic_str_arrays[i].dim2 = 0;
        basic_str_arrays[i].is_string = 1;
    }
#endif
#endif
}

/**
 * @brief Allocate (or reset) the BASIC working-set arena and bind
 *        each sub-region to its global pointer.
 *
 * On FR5994 with MEMORY_MODEL=large the AUTO-tier request routes to
 * HIFRAM (the threshold is 1 KB); on smaller parts it falls back to
 * SRAM.
 *
 * @return 0 on success, -1 on allocation failure.
 */
static int
basic_alloc_state(void)
{
    uint16_t i;

    if (basic_arena_ready) {
        (void)tiku_arena_reset(&basic_arena);
    } else {
        (void)tiku_tier_init();
        if (tiku_tier_arena_create(&basic_arena, TIKU_MEM_AUTO,
                                    BASIC_ARENA_BYTES, 0xBAu)
            != TIKU_MEM_OK) {
            return -1;
        }
        basic_arena_ready = 1;
    }

    /* The arena is BASIC's hot working set -- the line table, variables and
     * stacks are written on every statement.  It MUST be byte-writable RAM.
     * If AUTO fell back to the NVM tier (because the SRAM tier was too small
     * for BASIC_ARENA_BYTES), refuse here: on parts whose NVM is program-op
     * (RP2350 QSPI flash, Ambiq MRAM) the first store would hard-fault and
     * wedge the board at `basic` entry instead of failing cleanly.  The fix
     * is to size TIKU_TIER_SRAM_SIZE for the part (see the Makefile). */
    if (basic_arena.tier == TIKU_MEM_NVM) {
        return -1;
    }

    prog = (basic_line_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_line_t) * TIKU_BASIC_PROGRAM_LINES));
    basic_vars = (long *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(long) * BASIC_VAR_TABLE_LEN));
    basic_namedvar_names = (char (*)[TIKU_BASIC_NAMEDVAR_LEN])
        tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)BASIC_ARENA_NAMEDVAR_BYTES);
    gosub_stack = (uint16_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(uint16_t) * TIKU_BASIC_GOSUB_DEPTH));
    for_stack = (basic_for_frame_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_for_frame_t) *
                                TIKU_BASIC_FOR_DEPTH));
    loop_stack = (basic_loop_frame_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_loop_frame_t) *
                                TIKU_BASIC_LOOP_DEPTH));
    basic_everys = (basic_every_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_every_t) *
                                TIKU_BASIC_EVERY_MAX));
    basic_onchgs = (basic_onchg_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_onchg_t) *
                                TIKU_BASIC_ONCHG_MAX));
#if TIKU_BASIC_STRVARS_ENABLE
    basic_strvars = (char **)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(char *) * BASIC_VAR_TABLE_LEN));
    basic_namedstrvar_names = (char (*)[TIKU_BASIC_NAMEDVAR_LEN])
        tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)BASIC_ARENA_NAMEDVAR_BYTES);
    basic_str_heap = (char *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)TIKU_BASIC_STR_HEAP_BYTES);
#if TIKU_BASIC_BIGBUF_COUNT > 0
    {
        int bi;
        for (bi = 0; bi < TIKU_BASIC_BIGBUF_COUNT; bi++)
            basic_bigbuf[bi] = (char *)tiku_arena_alloc(&basic_arena,
                (tiku_mem_arch_size_t)TIKU_BASIC_BIGBUF_SIZE);
    }
#endif
#endif
#if TIKU_BASIC_DEFN_ENABLE
    basic_defns = (basic_defn_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_defn_t) * TIKU_BASIC_DEFN_MAX));
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
    basic_arrays = (basic_array_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_array_t) * 26u));
#if TIKU_BASIC_STRVARS_ENABLE
    basic_str_arrays = (basic_array_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_array_t) * 26u));
#endif
#endif

    if (prog == NULL || basic_vars == NULL || gosub_stack == NULL ||
        for_stack == NULL || loop_stack == NULL ||
        basic_everys == NULL || basic_onchgs == NULL ||
        basic_namedvar_names == NULL
#if TIKU_BASIC_STRVARS_ENABLE
        || basic_strvars == NULL || basic_str_heap == NULL ||
        basic_namedstrvar_names == NULL
#if TIKU_BASIC_BIGBUF_COUNT > 0
        || basic_bigbuf[TIKU_BASIC_BIGBUF_COUNT - 1] == NULL
#endif
#endif
#if TIKU_BASIC_DEFN_ENABLE
        || basic_defns == NULL
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
        || basic_arrays == NULL
#if TIKU_BASIC_STRVARS_ENABLE
        || basic_str_arrays == NULL
#endif
#endif
        ) {
        return -1;
    }

    /* Capture the arena high-water mark just past the fixed working set, so
     * basic_clear_vars() can reclaim DIMmed array element storage by rewinding
     * to here on NEW / RUN / LOAD. */
    basic_arena_mark = basic_arena.offset;

    /* Arena reset doesn't zero memory, so initialise explicitly: clear the
     * line table here, then reset every variable via the shared helper (which
     * also rewinds to the mark just captured -- a no-op on this first pass). */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) prog[i].number = 0;
    basic_clear_vars();
    return 0;
}
