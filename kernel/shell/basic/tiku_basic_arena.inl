/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_arena.inl - arena allocation for the BASIC working set.
 *
 * Not a standalone unit; included from tiku_basic.c.  Computes the footprint from
 * the configured limits, then lazily allocates every table out of the AUTO tier.
 * Each session resets and re-allocates, so the feature set costs no permanent BSS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* ARENA SIZING                                                              */
/*---------------------------------------------------------------------------*/

/* Total var-table width = 26 single-letter A..Z slots + named pool.
 * Used when sizing the arena and when walking var arrays. */
#define BASIC_VAR_TABLE_LEN  (26u + (unsigned)TIKU_BASIC_NAMEDVAR_MAX)

/* Compute the arena size the configured limits need, with a
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
 * the reservation is simply enough total headroom for one or two reasonably-
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
        sizeof(uint16_t)           * TIKU_BASIC_PROGRAM_LINES +   /* A3 line index */ \
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

/*
 * THE ARENA MUST FIT ITS TIER POOL -- AT BUILD TIME.
 *
 * basic_alloc_state() asks the AUTO tier for BASIC_ARENA_BYTES in one carve.
 * Two numbers therefore have to agree, and until v0.06 nothing compared them:
 * this figure, computed from the capacity macros below, and the tier pool size
 * the Makefile passes per MCU.  When the pool was too small the build stayed
 * completely clean and the failure appeared only when a user typed `basic` on
 * the board and got "out of memory" -- which is how
 * `MCU=nrf54l15 TIKU_THREADS_ENABLE=1` shipped with a 13.7 KB shortfall
 * (65,536 B pool against a 79,232 B request; the sizing comment beside it had
 * tallied the line table, big buffers and string heap but omitted the 16 KB
 * DIM array reserve).
 *
 * The arena is the ONLY production consumer of the tier pool -- the other
 * potential one, tiku_proc_mem, has no callers outside its own module -- so
 * these asserts are exact today, not merely necessary.  If a second consumer
 * ever appears, the pool has to cover both and this becomes a lower bound.
 *
 * Which pool applies follows AUTO's resolution order (HIFRAM -> SRAM -> NVM,
 * kernel/memory/tiku_tier.c): on MSP430 the request clears the 1 KB HIFRAM
 * threshold and lands in the upper FRAM bank; on the ARM parts no HIFRAM tier
 * exists, so it lands in SRAM.  AUTO's third option, NVM, is deliberately
 * refused in basic_alloc_state() -- the arena is rewritten on every statement
 * and a store into MRAM or QSPI flash would fault -- so it is never a
 * legitimate home and is not asserted against.  Host builds have no
 * meaningful pool and are left alone.
 */
#if defined(PLATFORM_MSP430)
_Static_assert(BASIC_ARENA_BYTES <= TIKU_TIER_HIFRAM_SIZE,
               "BASIC arena does not fit the HIFRAM tier pool -- raise "
               "TIKU_TIER_HIFRAM_SIZE or lower TIKU_BASIC_PROGRAM_LINES");
#elif defined(PLATFORM_NORDIC) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_RP2350)
_Static_assert(BASIC_ARENA_BYTES <= TIKU_TIER_SRAM_SIZE,
               "BASIC arena does not fit the SRAM tier pool -- raise "
               "TIKU_TIER_SRAM_SIZE for this MCU in the Makefile, or lower "
               "TIKU_BASIC_PROGRAM_LINES");
#endif

/*---------------------------------------------------------------------------*/
/* ALLOCATION                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset the BASIC variable namespace to its just-entered state.
 *
 * Clears every user-visible binding -- scalars, strings and their heap, arrays,
 * DEF FN -- and rewinds the arena to basic_arena_mark so DIMmed element storage
 * is reclaimed.  The program line table is deliberately left alone.
 *
 * @note Shared by basic_alloc_state(), NEW, RUN and LOAD so all four agree on
 *       what "fresh variables" means, and so re-DIMming across runs does not
 *       trip "array already DIMmed".
 */
static void
basic_clear_vars(void)
{
    uint16_t i;

    /* Reclaim DIMmed array element storage: it is the only thing allocated
     * from the arena past the mark, so this rewind frees all of it at once. */
    basic_arena.offset = basic_arena_mark;

#if TIKU_BASIC_SUBS_ENABLE
    basic_sub_result = 0;                    /* SUB return register (F3) */
#endif
    basic_named_mru[0] = -1;                 /* named-slot MRU (A3 #3) */
    basic_named_mru[1] = -1;
    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) basic_vars[i] = 0;
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        basic_namedvar_names[i][0] = '\0';
        basic_namedvar_const[i]    = 0;      /* CONST read-only flags (F4) */
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

    /* Attach the arena to the owning (shell) process so ps and
     * /proc/<pid>/sram_used report BASIC's real footprint -- measured
     * from the bump pointer, not self-declared.  Idempotent. */
    {
        struct tiku_process *self = TIKU_THIS();
        if (self != NULL) {
            tiku_process_attach_mem_arena(self, &basic_arena);
        }
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
    basic_line_order = (uint16_t *)tiku_arena_alloc(&basic_arena,   /* A3 */
        (tiku_mem_arch_size_t)(sizeof(uint16_t) * TIKU_BASIC_PROGRAM_LINES));
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

    if (prog == NULL || basic_line_order == NULL ||
        basic_vars == NULL || gosub_stack == NULL ||
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
    basic_line_index_ok = 0;                  /* A3: line index not built yet */
    basic_symreg_ok     = 0;                  /* A3 #2: SUB/label registry too */
    basic_clear_vars();
    return 0;
}
