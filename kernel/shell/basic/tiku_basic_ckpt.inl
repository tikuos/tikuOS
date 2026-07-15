/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ckpt.inl - F1: power-failure-transparent RUN.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c AFTER
 * tiku_basic_persist.inl (it borrows the MPU-unlock discipline) and
 * tiku_basic_arena.inl (it needs BASIC_VAR_TABLE_LEN and the frame
 * struct sizes to bound the checkpoint buffer at compile time).
 *
 * The single most important architectural fact about this interpreter is that
 * its entire machine state is reified in file-static globals: the program
 * counter (basic_pc), the FOR / GOSUB / loop stacks, the scalar + string
 * variables, and the error / DATA / PRNG state.  That makes it CHECKPOINTABLE.
 *
 *   PERSIST ON    arms checkpointing.
 *   <run>         at each yield boundary the reified state is written to a
 *                 durable NVM slot ("bck"), gate-last.
 *   RUN RESUME    restores that state and continues from basic_pc, mid-loop,
 *                 across a reset or power cut -- instead of restarting.
 *
 * This is distinct from SAVE / LOAD: those persist the PROGRAM TEXT, this
 * persists the RUNNING MACHINE ("energy scheduled, not survived", expressed as
 * a language feature).  No other embedded BASIC makes the claim.
 *
 * --- Why the serialization is pointer-free ------------------------------------
 * Almost every piece of state is a value: basic_pc (u16), the stacks (line
 * numbers, var indices, longs), basic_vars[] (long[]), the named-var name
 * tables (fixed char[]).  The ONLY pointers are the string variables, which
 * point INTO basic_str_heap.  So we serialize the used heap prefix verbatim and
 * store each string pointer as a HEAP OFFSET (ptr - basic_str_heap; 0xFFFF ==
 * NULL/unbound).  On resume the arena is freshly allocated (stable, in-order
 * sub-allocations), the heap prefix is copied back, and each offset is rebound
 * to basic_str_heap + off.  No arena-base assumption, no pointer rebasing.
 *
 * --- Why a power cut needs no detection --------------------------------------
 * Checkpoints are taken at yield boundaries while armed, and the durable slot
 * is INVALIDATED on any *orderly* termination (END / STOP / fell off the end /
 * Ctrl-C break).  A power cut is precisely the case where that orderly-clear
 * never executes -- so the last per-batch checkpoint survives on FRAM and RUN
 * RESUME finds it.  Resume replays at most one batch of lines (the window
 * between the last checkpoint and the cut), so side effects in that window
 * repeat; this is inherent to checkpoint/replay and is documented.
 *
 * --- Storage backends + torn-write safety ------------------------------------
 * The durable slot has TWO backings, chosen by BASIC_NVM_ON_REGION (the same
 * split as the saved program -- see tiku_basic_config.h):
 *
 *   Byte-writable (MSP430 .persistent FRAM; Nordic/host .bss = session-only):
 *   [u32 gate][u32 version][u32 len][payload].  A save invalidates the gate,
 *   serializes the payload, stamps version + len, then writes the magic gate
 *   LAST -- single-word FRAM stores commit atomically, so a cut mid-write
 *   leaves the gate invalid.
 *
 *   Carved NVM region (Ambiq MRAM, RP2350 flash): a fixed slot at the TOP of
 *   the region's reserved tail (the saved program owns the base).  These media
 *   are program-op (bootrom / sector erase+program), so multi-call gate-last
 *   ordering would cost one extra sector erase per save on flash -- instead
 *   the image is [u32 magic][u32 version][u32 len][u32 crc][payload], built in
 *   a RAM scratch and committed with ONE tiku_tier_nvm_write.  Validity =
 *   magic + version + CRC32(payload): any torn write (including a power cut
 *   mid-sector-erase) fails the CRC and RESUME restarts instead.
 *
 * Either way: losing an in-flight checkpoint (restart instead of resume) is
 * acceptable; resuming garbage is not.  The slot is sized to the compile-time
 * worst case, so serialization can never overflow it and there is no
 * "checkpoint too large" runtime path for the core state.  (v1 core state =
 * scalars; arrays / EVERY / ON CHANGE are not yet captured -- PERSIST ON warns
 * if any are live.)
 *
 * --- Checkpoint cadence ------------------------------------------------------
 * TIKU_BASIC_CKPT_INTERVAL_S (config) paces saves: 0 = every yield batch
 * (FRAM-class endurance), nonzero = at most one save per interval (flash
 * sector-erase wear, MRAM bootrom-call jitter).  The replay window after a
 * power cut grows accordingly -- see the config comment for the arithmetic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_PERSIST_RUN_ENABLE

/* Host-harness fallbacks: the on-target build gets both from
 * tiku_basic_config.h; the host round-trip test defines its config by hand and
 * exercises the byte-writable path. */
#ifndef BASIC_NVM_ON_REGION
#define BASIC_NVM_ON_REGION 0
#endif
#ifndef TIKU_BASIC_CKPT_INTERVAL_S
#define TIKU_BASIC_CKPT_INTERVAL_S 0
#endif

/*---------------------------------------------------------------------------*/
/* SLOT FORMAT + SIZING                                                       */
/*---------------------------------------------------------------------------*/

/* Distinct from TIKU_PERSIST_MAGIC so a checkpoint slot is never confused with
 * a program-store entry, and from BASIC_REGION_MAGIC ('BASP'). */
#define BASIC_CKPT_MAGIC    0x424B5054u   /* 'BKPT' */
#define BASIC_CKPT_VERSION  5u            /* 2: +program id; 3: +SUB/scope/DEF FN;
                                           * 4: string scope slots + RESULT (F3);
                                           * 5: EVERY + ON CHANGE + arrays (F1 f/u) */
#define BASIC_CKPT_HDR      12u           /* [gate u32][version u32][len u32] */
#define BASIC_CKPT_RGN_HDR  16u           /* [magic][version][len][crc]       */

#if TIKU_BASIC_STRVARS_ENABLE
#define BASIC_CKPT_STR_MAX ( \
      (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX  /* str names */ \
    + sizeof(uint16_t)                       /* heap_pos           */ \
    + (size_t)TIKU_BASIC_STR_HEAP_BYTES      /* full heap prefix   */ \
    + sizeof(uint16_t) * BASIC_VAR_TABLE_LEN)/* strvar offsets     */
#else
#define BASIC_CKPT_STR_MAX 0u
#endif

#if TIKU_BASIC_SUBS_ENABLE
#define BASIC_CKPT_SUBS_MAX ( \
      1u + sizeof(basic_frame_t) * TIKU_BASIC_CALL_DEPTH   /* SUB call frames */ \
    + 1u + sizeof(basic_scope_t) * TIKU_BASIC_SCOPE_MAX    /* LOCAL scope     */ \
    + sizeof(long))                                        /* RESULT register */
#else
#define BASIC_CKPT_SUBS_MAX 0u
#endif
#if TIKU_BASIC_DEFN_ENABLE
#define BASIC_CKPT_DEFN_MAX_B (1u + sizeof(basic_defn_t) * TIKU_BASIC_DEFN_MAX)
#else
#define BASIC_CKPT_DEFN_MAX_B 0u
#endif

/* F1 follow-up budgets: EVERY timers, ON CHANGE regs, and DIMmed arrays. */
#if TIKU_BASIC_EVERY_MAX > 0
#define BASIC_CKPT_EVERY_B (1u + (size_t)TIKU_BASIC_EVERY_MAX *                \
    (sizeof(long) + TIKU_BASIC_EVERY_STMT_LEN))
#else
#define BASIC_CKPT_EVERY_B 0u
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
#define BASIC_CKPT_ONCHG_B (1u + (size_t)TIKU_BASIC_ONCHG_MAX *                \
    (40u + sizeof(long) + sizeof(uint16_t) + 1u))
#else
#define BASIC_CKPT_ONCHG_B 0u
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
/* Fixed data budget for the array checkpoint; DIMmed arrays whose serialized
 * bytes overflow it write present=0 and reset on resume (not corruption).
 * Plus a present byte + two dims across the 26 numeric + 26 string slots. */
#define BASIC_CKPT_ARR_BYTES ((size_t)TIKU_BASIC_STR_HEAP_BYTES)
#define BASIC_CKPT_ARR_B     (52u * (1u + 2u * sizeof(uint16_t)) + \
                              BASIC_CKPT_ARR_BYTES)
#else
#define BASIC_CKPT_ARR_BYTES 0u
#define BASIC_CKPT_ARR_B     0u
#endif

/* Compile-time upper bound on the serialized payload.  Generous fixed slack
 * absorbs struct padding differences so the buffer is never undersized. */
#define BASIC_CKPT_PAYLOAD_MAX ( \
      sizeof(uint32_t)                                             /* prog identity */ \
    + 24u                                                          /* pc + flags */ \
    + 1u + sizeof(uint16_t) * TIKU_BASIC_GOSUB_DEPTH               /* gosub */ \
    + 1u + sizeof(basic_for_frame_t) * TIKU_BASIC_FOR_DEPTH        /* for */ \
    + 1u + sizeof(basic_loop_frame_t) * TIKU_BASIC_LOOP_DEPTH      /* loop */ \
    + sizeof(long) * BASIC_VAR_TABLE_LEN                           /* numeric vars */ \
    + (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX    /* numeric names */ \
    + BASIC_CKPT_STR_MAX \
    + BASIC_CKPT_SUBS_MAX                                          /* SUB frames + scope */ \
    + BASIC_CKPT_DEFN_MAX_B                                        /* DEF FN table */ \
    + BASIC_CKPT_EVERY_B                                           /* EVERY timers */ \
    + BASIC_CKPT_ONCHG_B                                           /* ON CHANGE regs */ \
    + BASIC_CKPT_ARR_B                                             /* DIMmed arrays */ \
    + 64u)                                                         /* err/data/prng + slack */

#define TIKU_BASIC_CKPT_BYTES  (BASIC_CKPT_HDR + BASIC_CKPT_PAYLOAD_MAX)

#if BASIC_NVM_ON_REGION
/* Region slot: fixed offset at the TOP of the reserved tail, 4 KB-aligned so a
 * torn flash sector erase during a checkpoint can never blast bytes of the
 * saved-program slot at the tail base (harmless-but-tidy on MRAM, which has no
 * erase granule). */
#define BASIC_CKPT_SLOT_BYTES \
    (((BASIC_CKPT_RGN_HDR + BASIC_CKPT_PAYLOAD_MAX) + 4095u) & ~(size_t)4095u)
_Static_assert(TIKU_BASIC_SAVE_BUF_BYTES + 8u + BASIC_CKPT_SLOT_BYTES
                   <= TIKU_NVM_RESERVED_BYTES,
               "program + checkpoint slots overflow the reserved region tail");

/* RAM staging for the single-call region write (BASIC_SCRATCH: .ssram on
 * Ambiq, .bss on RP2350 -- transient, rebuilt on every save). */
static BASIC_SCRATCH uint8_t
    basic_ckpt_scratch[BASIC_CKPT_RGN_HDR + BASIC_CKPT_PAYLOAD_MAX];

/** @brief Base of the checkpoint slot inside the reserved tail, or NULL. */
static uint8_t *
basic_ckpt_region_slot(void)
{
    uint8_t *tail = basic_region_slot();      /* tiku_basic_persist.inl */
    if (tail == NULL) {
        return NULL;
    }
    return tail + (TIKU_NVM_RESERVED_BYTES - BASIC_CKPT_SLOT_BYTES);
}
#else
/* Byte-writable slot.  .persistent FRAM on MSP430 (durable); plain .bss on
 * Nordic/host (session-only -- matches SAVE's durability envelope there). */
static BASIC_NVM_PERSISTENT uint8_t basic_ckpt_buf[TIKU_BASIC_CKPT_BYTES];
#endif

/*---------------------------------------------------------------------------*/
/* BYTE CURSORS                                                               */
/*---------------------------------------------------------------------------*/

typedef struct { uint8_t *base; size_t pos, cap; int err; } basic_ckpt_wr_t;
typedef struct { const uint8_t *base; size_t pos, len; int err; } basic_ckpt_rd_t;

static void
ckpt_w(basic_ckpt_wr_t *w, const void *src, size_t n)
{
    if (w->err || w->pos + n > w->cap) { w->err = 1; return; }
    memcpy(w->base + w->pos, src, n);
    w->pos += n;
}

static void
ckpt_r(basic_ckpt_rd_t *r, void *dst, size_t n)
{
    if (r->err || r->pos + n > r->len) { r->err = 1; memset(dst, 0, n); return; }
    memcpy(dst, r->base + r->pos, n);
    r->pos += n;
}

/*---------------------------------------------------------------------------*/
/* PROGRAM IDENTITY (binds a checkpoint to the program it was captured from)   */
/*---------------------------------------------------------------------------*/

/* Incremental CRC-32 (reflected, poly 0xEDB88320): seed with 0xFFFFFFFF and
 * XOR the result with 0xFFFFFFFF to finalize.  Backs both the program identity
 * below and the region-slot payload CRC (basic_ckpt_crc32). */
static uint32_t
basic_crc32_step(uint32_t c, const uint8_t *p, size_t n)
{
    size_t i;
    int    k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
        }
    }
    return c;
}

/**
 * @brief CRC-32 fingerprint of the in-memory program (each line's number +
 *        text, in ascending line order -- the shape LIST / SAVE emit).
 *
 * The checkpoint stores the fingerprint of the program that was RUNNING when it
 * was captured; RESUME recomputes it against the program actually loaded and
 * rejects the slot on mismatch, so a basic_pc / GOSUB / FOR stack full of line
 * numbers is never replayed against a different or edited program (which would
 * jump to a line that means something else, or nothing).  Empty program -> 0.
 */
static uint32_t
basic_prog_identity(void)
{
    uint32_t c   = 0xFFFFFFFFu;
    uint16_t cur = 0;

    if (prog == NULL) {
        return 0u;
    }
    while (1) {
        int      idx = prog_next_index(cur);
        uint16_t num;
        if (idx < 0) {
            break;
        }
        num = prog[idx].number;
        c = basic_crc32_step(c, (const uint8_t *)&num, sizeof num);
        c = basic_crc32_step(c, (const uint8_t *)prog[idx].text,
                             strlen(prog[idx].text) + 1u);   /* incl. NUL sep */
        if (num == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(num + 1);
    }
    return c ^ 0xFFFFFFFFu;
}

/*---------------------------------------------------------------------------*/
/* SERIALIZE / DESERIALIZE                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Serialize the reified execution state into @p w.
 *
 * Order matters only in that it must mirror basic_ckpt_read().  The string
 * block writes heap_pos and the heap prefix BEFORE the strvar offsets, so the
 * reader can validate each offset against the restored heap length.
 */
static void
basic_ckpt_write(basic_ckpt_wr_t *w)
{
    uint16_t i;
    uint8_t  u8;
    uint32_t pid = basic_prog_identity();

    ckpt_w(w, &pid, sizeof(pid));            /* program this state belongs to */
    ckpt_w(w, &basic_pc, sizeof(basic_pc));
    u8 = (uint8_t)basic_pc_set; ckpt_w(w, &u8, 1);
    u8 = (uint8_t)basic_trace;  ckpt_w(w, &u8, 1);
    u8 = basic_ckpt_armed;      ckpt_w(w, &u8, 1);

    ckpt_w(w, &gosub_sp, 1);
    for (i = 0; i < gosub_sp; i++) ckpt_w(w, &gosub_stack[i], sizeof(uint16_t));
    ckpt_w(w, &for_sp, 1);
    for (i = 0; i < for_sp; i++) ckpt_w(w, &for_stack[i], sizeof(basic_for_frame_t));
    ckpt_w(w, &loop_sp, 1);
    for (i = 0; i < loop_sp; i++) ckpt_w(w, &loop_stack[i], sizeof(basic_loop_frame_t));

    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) ckpt_w(w, &basic_vars[i], sizeof(long));
    ckpt_w(w, basic_namedvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);

#if TIKU_BASIC_STRVARS_ENABLE
    ckpt_w(w, basic_namedstrvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);
    ckpt_w(w, &basic_str_heap_pos, sizeof(basic_str_heap_pos));
    ckpt_w(w, basic_str_heap, basic_str_heap_pos);          /* used prefix only */
    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) {
        uint16_t off = (basic_strvars[i] == NULL)
                     ? 0xFFFFu
                     : (uint16_t)(basic_strvars[i] - basic_str_heap);
        ckpt_w(w, &off, sizeof(off));
    }
#endif

    ckpt_w(w, &basic_err_handler, sizeof(basic_err_handler));
    ckpt_w(w, &basic_err_pc, sizeof(basic_err_pc));
    ckpt_w(w, &basic_err, sizeof(basic_err));
    ckpt_w(w, &basic_erl, sizeof(basic_erl));
    ckpt_w(w, &basic_data_idx, sizeof(basic_data_idx));
    ckpt_w(w, &basic_data_off, sizeof(basic_data_off));
    ckpt_w(w, &basic_prng_state, sizeof(basic_prng_state));
    ckpt_w(w, &basic_prng_seeded, sizeof(basic_prng_seeded));

#if TIKU_BASIC_SUBS_ENABLE
    /* SUB call frames + LOCAL restore stack: a program checkpointed mid-CALL
     * resumes inside the SUB with its LOCALs intact, instead of the frame
     * silently vanishing (ENDSUB falling through, LOCALs never restored). */
    ckpt_w(w, &basic_call_sp, 1);
    for (i = 0; i < basic_call_sp; i++)
        ckpt_w(w, &basic_frames[i], sizeof(basic_frame_t));
    ckpt_w(w, &basic_scope_sp, 1);
    for (i = 0; i < basic_scope_sp; i++) {
        /* Serialize old_str as a heap OFFSET (a raw pointer would not survive
         * a power cut), mirroring the strvar block above. */
        basic_scope_t *s = &basic_scope[i];
        uint16_t soff = (!s->is_str || s->old_str == NULL)
                      ? 0xFFFFu
                      : (uint16_t)(s->old_str - basic_str_heap);
        ckpt_w(w, &s->idx,    sizeof(s->idx));
        ckpt_w(w, &s->is_str, sizeof(s->is_str));
        ckpt_w(w, &s->old,    sizeof(s->old));
        ckpt_w(w, &soff,      sizeof(soff));
    }
    ckpt_w(w, &basic_sub_result, sizeof(basic_sub_result));
#endif
#if TIKU_BASIC_DEFN_ENABLE
    /* DEF FN table: active definitions only (lookup is by name, so restoring
     * them compacted into slots 0..n-1 is fine).  Without this, resume clears
     * basic_defns and every post-resume FN...() errors. */
    {
        uint8_t nd = 0, k;
        for (k = 0; k < TIKU_BASIC_DEFN_MAX; k++)
            if (basic_defns[k].name[0]) nd++;
        ckpt_w(w, &nd, 1);
        for (k = 0; k < TIKU_BASIC_DEFN_MAX; k++)
            if (basic_defns[k].name[0])
                ckpt_w(w, &basic_defns[k], sizeof(basic_defn_t));
    }
#endif

#if TIKU_BASIC_EVERY_MAX > 0
    /* F1 follow-up: EVERY timer slots.  Only interval + stmt are saved;
     * next_due is re-armed relative to the current clock on restore (an
     * absolute deadline is meaningless after the clock resets). */
    {
        uint8_t n = 0, k;
        for (k = 0; k < TIKU_BASIC_EVERY_MAX; k++) if (basic_everys[k].active) n++;
        ckpt_w(w, &n, 1);
        for (k = 0; k < TIKU_BASIC_EVERY_MAX; k++) {
            if (!basic_everys[k].active) continue;
            ckpt_w(w, &basic_everys[k].interval_ms, sizeof(long));
            ckpt_w(w, basic_everys[k].stmt, (size_t)TIKU_BASIC_EVERY_STMT_LEN);
        }
    }
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    /* ON CHANGE registrations: path + baseline value + handler.  The runtime
     * node cache / armed / pending (F2) are re-derived by the mode tick. */
    {
        uint8_t n = 0, k;
        for (k = 0; k < TIKU_BASIC_ONCHG_MAX; k++) if (basic_onchgs[k].active) n++;
        ckpt_w(w, &n, 1);
        for (k = 0; k < TIKU_BASIC_ONCHG_MAX; k++) {
            if (!basic_onchgs[k].active) continue;
            ckpt_w(w, basic_onchgs[k].path, sizeof(basic_onchgs[k].path));
            ckpt_w(w, &basic_onchgs[k].last_value, sizeof(long));
            ckpt_w(w, &basic_onchgs[k].handler_line, sizeof(uint16_t));
            ckpt_w(w, &basic_onchgs[k].is_gosub, 1);
        }
    }
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
    /* DIMmed arrays, budget-capped (BASIC_CKPT_ARR_BYTES).  Each slot writes a
     * present byte; a DIMmed array that would overflow the budget writes
     * present=0 and is left to reset on resume (documented limitation, not a
     * corruption).  Numeric data is verbatim longs; string elements are heap
     * offsets, mirroring the strvar block. */
    {
        uint16_t budget = BASIC_CKPT_ARR_BYTES;
        uint8_t  which, slot;
        for (which = 0; which < 2u; which++) {
            for (slot = 0; slot < 26u; slot++) {
#if TIKU_BASIC_STRVARS_ENABLE
                basic_array_t *a = which ? &basic_str_arrays[slot]
                                         : &basic_arrays[slot];
#else
                basic_array_t *a = &basic_arrays[slot];
                if (which) { uint8_t z = 0; ckpt_w(w, &z, 1); continue; }
#endif
                size_t  total = a->data ? (size_t)a->dim1 *
                                (size_t)(a->dim2 ? a->dim2 : 1u) : 0u;
                size_t  bytes = total * (which ? sizeof(uint16_t) : sizeof(long));
                uint8_t present = (a->data != NULL && bytes <= budget) ? 1u : 0u;
                ckpt_w(w, &present, 1);
                if (!present) continue;
                budget = (uint16_t)(budget - bytes);
                ckpt_w(w, &a->dim1, sizeof(uint16_t));
                ckpt_w(w, &a->dim2, sizeof(uint16_t));
#if TIKU_BASIC_STRVARS_ENABLE
                if (which) {
                    char **el = (char **)a->data;
                    size_t j;
                    for (j = 0; j < total; j++) {
                        uint16_t off = (el[j] == NULL) ? 0xFFFFu
                                     : (uint16_t)(el[j] - basic_str_heap);
                        ckpt_w(w, &off, sizeof(off));
                    }
                } else
#endif
                {
                    ckpt_w(w, a->data, total * sizeof(long));
                }
            }
        }
    }
#endif
}

/**
 * @brief Restore the reified execution state from a serialized payload.
 *
 * The stack depths and heap length are validated against the compiled limits:
 * a value out of range means the checkpoint came from an incompatible build (or
 * is corrupt), so the whole restore fails and the caller falls back to a fresh
 * RUN rather than jumping with a bogus stack pointer.
 *
 * @return 0 on a clean restore, -1 if the payload is short or inconsistent.
 */
static int
basic_ckpt_read(const uint8_t *payload, size_t len)
{
    basic_ckpt_rd_t r = { payload, 0, len, 0 };
    uint16_t i;
    uint8_t  u8, sp;
    uint32_t pid;

    /* Program-identity gate FIRST, before any state is touched: a checkpoint's
     * PC and GOSUB/FOR line numbers are only meaningful for the exact program
     * it was captured from.  If the loaded program differs (edited, or a
     * different SAVE clobbered the store since), reject cleanly -> fresh RUN. */
    ckpt_r(&r, &pid, sizeof(pid));
    if (r.err || pid != basic_prog_identity()) {
        return -1;
    }

    ckpt_r(&r, &basic_pc, sizeof(basic_pc));
    ckpt_r(&r, &u8, 1); basic_pc_set = u8;
    ckpt_r(&r, &u8, 1); basic_trace  = u8;
    ckpt_r(&r, &u8, 1); basic_ckpt_armed = u8;

    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_GOSUB_DEPTH) return -1;
    gosub_sp = sp;
    for (i = 0; i < sp; i++) ckpt_r(&r, &gosub_stack[i], sizeof(uint16_t));
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_FOR_DEPTH) return -1;
    for_sp = sp;
    for (i = 0; i < sp; i++) ckpt_r(&r, &for_stack[i], sizeof(basic_for_frame_t));
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_LOOP_DEPTH) return -1;
    loop_sp = sp;
    for (i = 0; i < sp; i++) ckpt_r(&r, &loop_stack[i], sizeof(basic_loop_frame_t));

    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) ckpt_r(&r, &basic_vars[i], sizeof(long));
    ckpt_r(&r, basic_namedvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);

#if TIKU_BASIC_STRVARS_ENABLE
    ckpt_r(&r, basic_namedstrvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);
    {
        uint16_t hp;
        ckpt_r(&r, &hp, sizeof(hp));
        if (hp > TIKU_BASIC_STR_HEAP_BYTES) return -1;
        basic_str_heap_pos = hp;
        ckpt_r(&r, basic_str_heap, hp);
        for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) {
            uint16_t off;
            ckpt_r(&r, &off, sizeof(off));
            if (off == 0xFFFFu) {
                basic_strvars[i] = NULL;
            } else if (off < hp) {
                basic_strvars[i] = basic_str_heap + off;
            } else {
                return -1;                  /* dangling offset -> reject */
            }
        }
    }
#endif

    ckpt_r(&r, &basic_err_handler, sizeof(basic_err_handler));
    ckpt_r(&r, &basic_err_pc, sizeof(basic_err_pc));
    ckpt_r(&r, &basic_err, sizeof(basic_err));
    ckpt_r(&r, &basic_erl, sizeof(basic_erl));
    ckpt_r(&r, &basic_data_idx, sizeof(basic_data_idx));
    ckpt_r(&r, &basic_data_off, sizeof(basic_data_off));
    ckpt_r(&r, &basic_prng_state, sizeof(basic_prng_state));
    ckpt_r(&r, &basic_prng_seeded, sizeof(basic_prng_seeded));

#if TIKU_BASIC_SUBS_ENABLE
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_CALL_DEPTH) return -1;
    basic_call_sp = sp;
    for (i = 0; i < sp; i++)
        ckpt_r(&r, &basic_frames[i], sizeof(basic_frame_t));
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_SCOPE_MAX) return -1;
    basic_scope_sp = sp;
    for (i = 0; i < sp; i++) {
        basic_scope_t *s = &basic_scope[i];
        uint16_t soff;
        ckpt_r(&r, &s->idx,    sizeof(s->idx));
        ckpt_r(&r, &s->is_str, sizeof(s->is_str));
        ckpt_r(&r, &s->old,    sizeof(s->old));
        ckpt_r(&r, &soff,      sizeof(soff));
        s->old_str = (soff == 0xFFFFu) ? NULL : basic_str_heap + soff;
    }
    ckpt_r(&r, &basic_sub_result, sizeof(basic_sub_result));
#endif
#if TIKU_BASIC_DEFN_ENABLE
    {
        uint8_t nd, k;
        ckpt_r(&r, &nd, 1);
        if (nd > TIKU_BASIC_DEFN_MAX) return -1;
        for (k = 0; k < TIKU_BASIC_DEFN_MAX; k++) basic_defns[k].name[0] = '\0';
        for (k = 0; k < nd; k++)
            ckpt_r(&r, &basic_defns[k], sizeof(basic_defn_t));
    }
#endif

#if TIKU_BASIC_EVERY_MAX > 0
    {
        uint8_t n, k;
        long    now_ms = (long)tiku_clock_time() * 1000L /
                         (long)TIKU_CLOCK_SECOND;
        ckpt_r(&r, &n, 1);
        if (n > TIKU_BASIC_EVERY_MAX) return -1;
        for (k = 0; k < n; k++) {
            long interval;
            ckpt_r(&r, &interval, sizeof(long));
            ckpt_r(&r, basic_everys[k].stmt, (size_t)TIKU_BASIC_EVERY_STMT_LEN);
            basic_everys[k].interval_ms = interval;
            basic_everys[k].next_due_ms = now_ms + interval;   /* re-armed */
            basic_everys[k].active      = 1;
        }
    }
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    {
        uint8_t n, k;
        ckpt_r(&r, &n, 1);
        if (n > TIKU_BASIC_ONCHG_MAX) return -1;
        for (k = 0; k < n; k++) {
            ckpt_r(&r, basic_onchgs[k].path, sizeof(basic_onchgs[k].path));
            ckpt_r(&r, &basic_onchgs[k].last_value, sizeof(long));
            ckpt_r(&r, &basic_onchgs[k].handler_line, sizeof(uint16_t));
            ckpt_r(&r, &basic_onchgs[k].is_gosub, 1);
            basic_onchgs[k].active = 1;
            /* node/armed/pending (F2) stay zero; the mode tick re-arms. */
        }
    }
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
    {
        uint8_t which, slot;
        for (which = 0; which < 2u; which++) {
            for (slot = 0; slot < 26u; slot++) {
                uint8_t        present;
                uint16_t       d1, d2;
                size_t         total;
                basic_array_t *a;
                ckpt_r(&r, &present, 1);
                if (!present) continue;
#if TIKU_BASIC_STRVARS_ENABLE
                a = which ? &basic_str_arrays[slot] : &basic_arrays[slot];
#else
                a = &basic_arrays[slot];
#endif
                ckpt_r(&r, &d1, sizeof(uint16_t));
                ckpt_r(&r, &d2, sizeof(uint16_t));
                total       = (size_t)d1 * (size_t)(d2 ? d2 : 1u);
                a->dim1     = d1;
                a->dim2     = d2;
                a->is_string = (uint8_t)which;
#if TIKU_BASIC_STRVARS_ENABLE
                if (which) {
                    char **el = (char **)tiku_arena_alloc(&basic_arena,
                        (tiku_mem_arch_size_t)(sizeof(char *) * total));
                    size_t j;
                    if (el == NULL) return -1;
                    for (j = 0; j < total; j++) {
                        uint16_t off;
                        ckpt_r(&r, &off, sizeof(off));
                        el[j] = (off == 0xFFFFu) ? NULL : basic_str_heap + off;
                    }
                    a->data = el;
                } else
#endif
                {
                    long *el = (long *)tiku_arena_alloc(&basic_arena,
                        (tiku_mem_arch_size_t)(sizeof(long) * total));
                    if (el == NULL) return -1;
                    ckpt_r(&r, el, total * sizeof(long));
                    a->data = el;
                }
            }
        }
    }
#endif

    return r.err ? -1 : 0;
}

/*---------------------------------------------------------------------------*/
/* DURABLE SLOT                                                               */
/*---------------------------------------------------------------------------*/

#if BASIC_NVM_ON_REGION

/** @brief CRC-32 (reflected, poly 0xEDB88320) over @p n bytes at @p p. */
static uint32_t
basic_ckpt_crc32(const uint8_t *p, size_t n)
{
    return basic_crc32_step(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}

/** @brief Invalidate the region checkpoint (magic := 0; one word program).
 *  Idempotent: skips the NVM write when the slot is already invalid, so
 *  repeated calls (e.g. per-line edits) cost no flash sector erase. */
static void
basic_ckpt_invalidate(void)
{
    uint8_t *slot = basic_ckpt_region_slot();
    uint32_t magic, z = 0u;

    if (slot == NULL) {
        return;
    }
    memcpy(&magic, slot, 4);
    if (magic != BASIC_CKPT_MAGIC) {
        return;                            /* already invalid -- no NVM write */
    }
    (void)tiku_tier_nvm_write(slot, &z, 4u);
}

/**
 * @brief Serialize the current state and commit it to the region slot in ONE
 *        backend write ([magic][ver][len][crc][payload]; CRC gates validity).
 * @return 0 on success, -1 on serialization failure or no usable backend.
 */
static int
basic_ckpt_save(void)
{
    basic_ckpt_wr_t w;
    uint8_t  *slot = basic_ckpt_region_slot();
    uint32_t  v;

    if (slot == NULL) {
        return -1;                     /* region backend absent this boot */
    }
    w.base = basic_ckpt_scratch + BASIC_CKPT_RGN_HDR;
    w.pos  = 0;
    w.cap  = sizeof(basic_ckpt_scratch) - BASIC_CKPT_RGN_HDR;
    w.err  = 0;
    basic_ckpt_write(&w);
    if (w.err) {
        return -1;
    }
    v = BASIC_CKPT_MAGIC;    memcpy(basic_ckpt_scratch + 0,  &v, 4);
    v = BASIC_CKPT_VERSION;  memcpy(basic_ckpt_scratch + 4,  &v, 4);
    v = (uint32_t)w.pos;     memcpy(basic_ckpt_scratch + 8,  &v, 4);
    v = basic_ckpt_crc32(basic_ckpt_scratch + BASIC_CKPT_RGN_HDR, w.pos);
    memcpy(basic_ckpt_scratch + 12, &v, 4);
    /* Single program op (tiku_tier_nvm_write owns the MPU window).  A power
     * cut mid-way -- including mid-sector-erase on flash -- leaves an image
     * whose CRC cannot match, so load() rejects it. */
    return (tiku_tier_nvm_write(slot, basic_ckpt_scratch,
                (tiku_mem_arch_size_t)(BASIC_CKPT_RGN_HDR + w.pos))
            == TIKU_MEM_OK) ? 0 : -1;
}

/**
 * @brief Restore state from the region slot if it holds a valid checkpoint.
 *        Reads in place (the region is memory-mapped: XIP flash / MRAM).
 * @return 0 if a checkpoint was restored, -1 if none / stale / torn.
 */
static int
basic_ckpt_load(void)
{
    const uint8_t *slot = basic_ckpt_region_slot();
    uint32_t magic, ver, len32, crc;

    if (slot == NULL) return -1;
    memcpy(&magic, slot,      4);
    if (magic != BASIC_CKPT_MAGIC) return -1;      /* no valid checkpoint */
    memcpy(&ver,   slot + 4,  4);
    memcpy(&len32, slot + 8,  4);
    memcpy(&crc,   slot + 12, 4);
    if (ver != BASIC_CKPT_VERSION) return -1;      /* incompatible firmware */
    if (len32 > BASIC_CKPT_SLOT_BYTES - BASIC_CKPT_RGN_HDR) return -1;
    if (basic_ckpt_crc32(slot + BASIC_CKPT_RGN_HDR, (size_t)len32) != crc) {
        return -1;                                 /* torn write -> restart */
    }
    return basic_ckpt_read(slot + BASIC_CKPT_RGN_HDR, (size_t)len32);
}

#else  /* byte-writable slot: gate-last multi-store */

/** @brief Invalidate the durable checkpoint (gate := 0).  Idempotent: skips the
 *  MPU-unlock + store when the gate is already clear (repeated per-edit calls
 *  stay free). */
static void
basic_ckpt_invalidate(void)
{
    uint16_t mpu;
    uint32_t gate, z = 0u;

    memcpy(&gate, basic_ckpt_buf, 4);
    if (gate != BASIC_CKPT_MAGIC) {
        return;                            /* already invalid */
    }
    mpu = tiku_mpu_unlock_nvm();
    memcpy(basic_ckpt_buf, &z, 4);
    tiku_mpu_lock_nvm(mpu);
}

/**
 * @brief Serialize the current state into the durable slot, gate-last.
 * @return 0 on success, -1 if serialization failed (buffer undersized -- can't
 *         happen for the compile-time-bounded core state, but checked anyway).
 */
static int
basic_ckpt_save(void)
{
    basic_ckpt_wr_t w;
    uint16_t mpu;
    uint32_t z = 0u, ver = BASIC_CKPT_VERSION, magic = BASIC_CKPT_MAGIC, len32;

    mpu = tiku_mpu_unlock_nvm();
    memcpy(basic_ckpt_buf, &z, 4);                 /* gate := 0 (invalidate) */
    w.base = basic_ckpt_buf + BASIC_CKPT_HDR;
    w.pos  = 0;
    w.cap  = TIKU_BASIC_CKPT_BYTES - BASIC_CKPT_HDR;
    w.err  = 0;
    basic_ckpt_write(&w);
    if (!w.err) {
        len32 = (uint32_t)w.pos;
        memcpy(basic_ckpt_buf + 4, &ver, 4);
        memcpy(basic_ckpt_buf + 8, &len32, 4);
        memcpy(basic_ckpt_buf, &magic, 4);         /* gate LAST -> now valid */
    }
    tiku_mpu_lock_nvm(mpu);
    return w.err ? -1 : 0;
}

/**
 * @brief Restore state from the durable slot if it holds a valid checkpoint.
 * @return 0 if a checkpoint was restored, -1 if none / stale / corrupt.
 */
static int
basic_ckpt_load(void)
{
    uint32_t gate, ver, len32;

    memcpy(&gate,  basic_ckpt_buf,     4);
    if (gate != BASIC_CKPT_MAGIC) return -1;       /* no valid checkpoint */
    memcpy(&ver,   basic_ckpt_buf + 4, 4);
    memcpy(&len32, basic_ckpt_buf + 8, 4);
    if (ver != BASIC_CKPT_VERSION) return -1;      /* incompatible firmware */
    if (len32 > TIKU_BASIC_CKPT_BYTES - BASIC_CKPT_HDR) return -1;
    return basic_ckpt_read(basic_ckpt_buf + BASIC_CKPT_HDR, (size_t)len32);
}

#endif /* BASIC_NVM_ON_REGION */

/*---------------------------------------------------------------------------*/
/* CADENCE (substrate-aware pacing of saves)                                  */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_CKPT_INTERVAL_S > 0
/* Second of the most recent save (or arming), from the kernel clock. */
static unsigned long basic_ckpt_last_s;
#endif

/**
 * @brief 1 when a checkpoint is due at this yield boundary.
 *
 * Interval 0 (byte-writable FRAM-class NVM) checkpoints every batch; a nonzero
 * interval (flash sector-erase wear, MRAM bootrom-call jitter) allows at most
 * one save per TIKU_BASIC_CKPT_INTERVAL_S, counted from PERSIST ON / the last
 * save.  See the config comment for the endurance arithmetic.
 */
static int
basic_ckpt_due(void)
{
#if TIKU_BASIC_CKPT_INTERVAL_S > 0
    return (unsigned long)(tiku_clock_seconds() - basic_ckpt_last_s)
               >= (unsigned long)TIKU_BASIC_CKPT_INTERVAL_S;
#else
    return 1;
#endif
}

/** @brief Restart the cadence interval (after a save, and at arming). */
static void
basic_ckpt_mark(void)
{
#if TIKU_BASIC_CKPT_INTERVAL_S > 0
    basic_ckpt_last_s = tiku_clock_seconds();
#endif
}

/*---------------------------------------------------------------------------*/
/* ARM / DISARM (the PERSIST statement)                                       */
/*---------------------------------------------------------------------------*/

/** @brief Arm (1) or disarm (0) checkpointing.  Disarming drops any slot;
 *  arming restarts the cadence interval so the first save lands one full
 *  interval after PERSIST ON. */
static void
basic_ckpt_arm(int on)
{
    basic_ckpt_armed = on ? 1u : 0u;
    if (on) {
        basic_ckpt_mark();
    } else {
        basic_ckpt_invalidate();
    }
}

/**
 * @brief Warn (once, at PERSIST ON) if state v1 does not yet checkpoint is live.
 *
 * Best-effort: arrays / EVERY / ON CHANGE created AFTER PERSIST ON are not
 * caught here, but the common case (PERSIST ON early in the program) is.  The
 * boundary is documented; this just keeps it from being silent.
 */
static void
basic_ckpt_warn_uncheckpointed(void)
{
    int warn = 0;
    uint16_t i;

#if TIKU_BASIC_ARRAYS_ENABLE
    for (i = 0; i < 26u && !warn; i++) if (basic_arrays[i].data) warn = 1;
#if TIKU_BASIC_STRVARS_ENABLE
    for (i = 0; i < 26u && !warn; i++) if (basic_str_arrays[i].data) warn = 1;
#endif
#endif
    for (i = 0; i < TIKU_BASIC_EVERY_MAX && !warn; i++)
        if (basic_everys[i].active) warn = 1;
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX && !warn; i++)
        if (basic_onchgs[i].active) warn = 1;

    if (warn) {
        SHELL_PRINTF(SH_YELLOW "note: PERSIST checkpoints scalars only; arrays / "
                     "EVERY / ON CHANGE do not yet resume" SH_RST "\n");
    }
}

#else  /* !TIKU_BASIC_PERSIST_RUN_ENABLE */

/* Stubs for the call sites the run loop / mode driver reach unconditionally
 * (basic_ckpt_armed is always 0 here, so these never actually fire).  arm() and
 * warn_uncheckpointed() are omitted: they are reached only from exec_persist's
 * enabled branch, which is compiled out on this build, so defining them would
 * just trip -Wunused-function. */
static int  basic_ckpt_save(void)       { return -1; }
static int  basic_ckpt_load(void)       { return -1; }
static void basic_ckpt_invalidate(void) { }
static int  basic_ckpt_due(void)        { return 0; }
static void basic_ckpt_mark(void)       { }

#endif /* TIKU_BASIC_PERSIST_RUN_ENABLE */
