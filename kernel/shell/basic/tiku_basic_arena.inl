/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_arena.inl - arena allocation for the BASIC working set.
 *
 * Sizes the arena from the configured limits and reserves it from a kernel
 * memory tier on first use; each later session resets it.  The working set
 * held there costs no static BSS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* ARENA SIZING                                                              */
/*---------------------------------------------------------------------------*/

/** Variable-table width: 26 single-letter slots A..Z plus the named pool. */
#define BASIC_VAR_TABLE_LEN  (26u + (unsigned)TIKU_BASIC_NAMEDVAR_MAX)

/* Per-feature byte counts that BASIC_ARENA_BYTES sums. */
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
/* DIM allocates element storage from the arena lazily, so the arena reserves
 * TIKU_BASIC_ARRAY_TOTAL_LONGS longs shared by every DIMmed array.  The
 * fallback below applies only when the config leaves it unset. */
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

/**
 * Arena bytes the configured limits need: the line table and its line-number
 * index, variables, stacks, the EVERY and ON CHANGE tables, strings, DEF FN,
 * arrays and big buffers, plus headroom for alignment between sub-allocations.
 */
#define BASIC_ARENA_BYTES                                                   \
    ((tiku_mem_arch_size_t)(                                                \
        sizeof(basic_line_t)       * TIKU_BASIC_PROGRAM_LINES +             \
        sizeof(uint16_t)           * TIKU_BASIC_PROGRAM_LINES +             \
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

#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_psram_arch.h>
#define BASIC_EXTERNAL_ATTACH() ((void)tiku_esp32c61_psram_attach())
#endif

/*
 * basic_alloc_state() requests BASIC_ARENA_BYTES in one contiguous
 * reservation, which must not exceed its tier pool's guaranteed size; the
 * asserts below fail the build when it does.
 *
 * The checks set a capacity floor, not the space free at run time: other
 * allocations share the same backing.  The request keeps AUTO's HIFRAM/SRAM
 * preference and never selects protected NVM; only TIKU_BASIC_ARENA_EXTERNAL
 * builds admit external memory.  On MSP430 large-model builds the arena is
 * past the HIFRAM threshold and lands in HIFRAM; elsewhere it uses SRAM, or
 * the external memory where that is admitted and SRAM is short.  Host builds
 * have no linker-derived capacity and are not checked.
 */
#if defined(TIKU_BASIC_ARENA_EXTERNAL)
/* External memory holds the arena, so the SRAM floor does not bound it; the
 * smallest PSRAM the part ships with does. */
_Static_assert(BASIC_ARENA_BYTES <= TIKU_BASIC_ARENA_EXTERNAL_MIN,
               "BASIC arena does not fit the smallest external memory -- "
               "lower TIKU_BASIC_PROGRAM_LINES");
#elif defined(PLATFORM_MSP430)
_Static_assert(BASIC_ARENA_BYTES <= TIKU_TIER_HIFRAM_SIZE,
               "BASIC arena does not fit the HIFRAM tier pool -- raise "
               "TIKU_TIER_HIFRAM_SIZE or lower TIKU_BASIC_PROGRAM_LINES");
#elif defined(TIKU_TIER_SRAM_DERIVED)
/* The SRAM tier is linker-derived here, so there is no compile-time size to
 * compare against; the floor stands in for it.  The same make-line figure
 * travels to the linker as --defsym=__tier_sram_floor, where the carve
 * fragment asserts the span against it. */
_Static_assert(BASIC_ARENA_BYTES <= TIKU_TIER_SRAM_MIN,
               "BASIC arena does not fit the SRAM tier floor -- raise "
               "TIKU_TIER_SRAM_MIN for this MCU in the Makefile, or lower "
               "TIKU_BASIC_PROGRAM_LINES");
#endif

/*---------------------------------------------------------------------------*/
/* ALLOCATION                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset the BASIC variable namespace to its just-entered state.
 *
 * Clears every user-visible binding -- scalars, strings and their heap, arrays,
 * DEF FN, CONST flags -- and rewinds the arena to basic_arena_mark, freeing
 * DIMmed element storage.  The program line table is left alone.
 *
 * @note Called by basic_alloc_state(), NEW, RUN, LOAD and the checkpoint
 *       restore; without it a second RUN of a program that DIMs A fails
 *       with "array A already DIMmed".
 */
static void
basic_clear_vars(void)
{
    uint16_t i;

    /* Reclaim DIMmed array element storage: it is the only thing allocated
     * from the arena past the mark, so this rewind frees all of it at once. */
    basic_arena.offset = basic_arena_mark;

#if TIKU_BASIC_SUBS_ENABLE
    basic_sub_result = 0;                    /* SUB return register */
#endif
    basic_named_mru[0] = -1;                 /* named-slot MRU */
    basic_named_mru[1] = -1;
    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) basic_vars[i] = 0;
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        basic_namedvar_names[i][0] = '\0';
        basic_namedvar_const[i]    = 0;      /* CONST read-only flags */
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
 * The first call creates the arena from the AUTO tier: HIFRAM on MSP430
 * large-model builds, SRAM elsewhere, or external memory when SRAM is short on
 * a TIKU_BASIC_ARENA_EXTERNAL build.  Later calls reset it.
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
#if BASIC_RECLAIM_ENABLE || defined(TIKU_BASIC_ARENA_EXTERNAL)
        tiku_mem_request_t options = {0};
#if defined(TIKU_BASIC_ARENA_EXTERNAL)
        /* SRAM first if the arena fits there, the external memory if not. */
        BASIC_EXTERNAL_ATTACH();
        options.flags = TIKU_MEM_ALLOW_EXTERNAL;
#endif
#if BASIC_RECLAIM_ENABLE
        if (basic_reclaim_register() != 0) return -1;
        options.owner = basic_reclaim_owner;
        options.owner_slot = 1;
#endif
        if (tiku_mem_arena_create(&basic_arena, BASIC_ARENA_BYTES, &options)
#else
        if (tiku_mem_arena_create(&basic_arena, BASIC_ARENA_BYTES, NULL)
#endif
            != TIKU_MEM_OK) {
            return -1;
        }
        basic_arena_ready = 1;
    }

    /* Attach the arena to the owning (shell) process: ps and
     * /proc/<pid>/sram_used report its bump-pointer usage.  Attaching again
     * is a no-op. */
    {
        struct tiku_process *self = TIKU_THIS();
#if BASIC_RECLAIM_ENABLE
        if (self) basic_reclaim_process = self;
        self = basic_reclaim_process;
#endif
        if (self != NULL) {
            tiku_process_attach_mem_arena(self, &basic_arena);
        }
    }

    /* Working requests exclude NVM; refuse an arena that landed there. */
    if (basic_arena.tier == TIKU_MEM_NVM) {
        return -1;
    }

    prog = (basic_line_t *)tiku_arena_alloc(&basic_arena,
        (tiku_mem_arch_size_t)(sizeof(basic_line_t) * TIKU_BASIC_PROGRAM_LINES));
    basic_line_order = (uint16_t *)tiku_arena_alloc(&basic_arena,
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
     * line table here, then reset every variable via the shared helper (its
     * rewind to the mark just captured is a no-op here). */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) prog[i].number = 0;
#if BASIC_RECLAIM_ENABLE
    /* The reclaim eligibility check reads these tables, possibly at the
     * prompt before the first RUN. */
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++)
        memset(&basic_everys[i], 0, sizeof basic_everys[i]);
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++)
        memset(&basic_onchgs[i], 0, sizeof basic_onchgs[i]);
#endif
    basic_line_index_ok = 0;                  /* line index needs a build */
    basic_symreg_ok     = 0;                  /* nor the SUB/label registry */
    basic_clear_vars();
    return 0;
}
