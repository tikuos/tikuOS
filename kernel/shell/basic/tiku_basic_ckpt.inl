/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ckpt.inl - power-failure-transparent RUN.
 *
 * The interpreter's machine state lives in file-static globals, which makes it
 * checkpointable: while PERSIST is armed, yield boundaries write that state to
 * durable memory, and RUN RESUME continues mid-loop across a power cut.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * The serialization is pointer-free.  Every piece of state is a value except
 * the pointers into basic_str_heap (string variables, string-array elements,
 * saved LOCAL strings), so the used heap prefix is written verbatim and each
 * such pointer as a heap offset (0xFFFF = NULL).  Resume copies the prefix
 * back and rebinds each offset, so nothing depends on where the arena sits.
 *
 * A power cut needs no detection.  Checkpoints are taken at yield boundaries
 * while armed, and every orderly end (END, STOP, the last line, Ctrl-C, NEW,
 * PERSIST OFF) invalidates the checkpoint; a cut skips that, so RUN RESUME
 * finds the last one.  Resume replays what ran since it -- at most one batch,
 * or one TIKU_BASIC_CKPT_INTERVAL_S and a batch where saves are paced -- so
 * the side effects of those lines repeat.
 *
 * Storage follows BASIC_NVM_ON_REGION, as the saved program does:
 *   region parts:  the /data file prog.ckpt, [payload][version][len][crc].
 *   MSP430, host:  a byte-writable buffer (durable FRAM on MSP430, .bss on
 *                  host), [gate][version][len][payload], gate written last.
 * A cut during a save leaves the previous checkpoint (region parts) or none
 * (the gate is cleared first), so a torn checkpoint is never resumed; one
 * from another program or payload version is rejected.  The payload has a
 * compile-time bound (BASIC_CKPT_PAYLOAD_MAX).
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
/* CHECKPOINT FORMAT AND SIZING                                              */
/*---------------------------------------------------------------------------*/

/* Gate word of the byte-writable checkpoint ('BKPT'), distinct from
 * TIKU_PERSIST_MAGIC so it is never taken for a persist-store entry. */
#define BASIC_CKPT_MAGIC    0x424B5054u
/** Payload layout version.  A checkpoint of any other version is rejected;
 *  bump it whenever basic_ckpt_write() changes what it writes. */
#define BASIC_CKPT_VERSION  6u
#define BASIC_CKPT_HDR      12u           /* [gate][version][len], u32 each */
#define BASIC_CKPT_RGN_HDR  16u           /* slack in the RP2350 staging
                                           * buffer, which holds the payload */

#if TIKU_BASIC_STRVARS_ENABLE
#define BASIC_CKPT_STR_MAX ( \
      (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX  /* $ names */ \
    + sizeof(uint16_t)                       /* heap_pos           */ \
    + (size_t)TIKU_BASIC_STR_HEAP_BYTES      /* full heap prefix   */ \
    + sizeof(uint16_t) * BASIC_VAR_TABLE_LEN)/* strvar offsets     */
#else
#define BASIC_CKPT_STR_MAX 0u
#endif

#if TIKU_BASIC_SUBS_ENABLE
#define BASIC_CKPT_SUBS_MAX ( \
      1u + sizeof(basic_frame_t) * TIKU_BASIC_CALL_DEPTH   /* call frames */ \
    + 1u + sizeof(basic_scope_t) * TIKU_BASIC_SCOPE_MAX    /* LOCAL scope */ \
    + sizeof(long))                                        /* RESULT register */
#else
#define BASIC_CKPT_SUBS_MAX 0u
#endif
#if TIKU_BASIC_DEFN_ENABLE
#define BASIC_CKPT_DEFN_MAX_B (1u + sizeof(basic_defn_t) * TIKU_BASIC_DEFN_MAX)
#else
#define BASIC_CKPT_DEFN_MAX_B 0u
#endif

/* Budgets for EVERY timers, ON CHANGE registrations and DIMmed arrays. */
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
/** Data budget for DIMmed arrays in a checkpoint: an array whose bytes would
 *  overflow it is written as absent and not restored.  BASIC_CKPT_ARR_B adds
 *  a present byte and two dims for each of the 52 array slots. */
#define BASIC_CKPT_ARR_BYTES ((size_t)TIKU_BASIC_STR_HEAP_BYTES)
#define BASIC_CKPT_ARR_B     (52u * (1u + 2u * sizeof(uint16_t)) + \
                              BASIC_CKPT_ARR_BYTES)
#else
#define BASIC_CKPT_ARR_BYTES 0u
#define BASIC_CKPT_ARR_B     0u
#endif

/* Compile-time upper bound on the serialized payload; the fixed slack
 * absorbs struct padding. */
#define BASIC_CKPT_PAYLOAD_MAX ( \
      sizeof(uint32_t)                               /* prog identity */ \
    + 24u                                            /* pc + flags */ \
    + 1u + sizeof(uint16_t) * TIKU_BASIC_GOSUB_DEPTH /* gosub */ \
    + 1u + sizeof(basic_for_frame_t) * TIKU_BASIC_FOR_DEPTH /* for */ \
    + 1u + sizeof(basic_loop_frame_t) * TIKU_BASIC_LOOP_DEPTH /* loop */ \
    + sizeof(long) * BASIC_VAR_TABLE_LEN             /* numeric vars */ \
    + (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX /* names */ \
    + (size_t)TIKU_BASIC_NAMEDVAR_MAX                /* CONST flags */ \
    + BASIC_CKPT_STR_MAX \
    + BASIC_CKPT_SUBS_MAX                            /* SUB frames + scope */ \
    + BASIC_CKPT_DEFN_MAX_B                          /* DEF FN table */ \
    + BASIC_CKPT_EVERY_B                             /* EVERY timers */ \
    + BASIC_CKPT_ONCHG_B                             /* ON CHANGE regs */ \
    + BASIC_CKPT_ARR_B                               /* DIMmed arrays */ \
    + 64u)                                           /* err/data/prng + slack */

#define TIKU_BASIC_CKPT_BYTES  (BASIC_CKPT_HDR + BASIC_CKPT_PAYLOAD_MAX)

#if BASIC_NVM_ON_REGION
/*
 * On region parts the checkpoint is the /data file BASIC_CKPT_FILE, with no
 * directory prefix, like prog.bas.  A store write is append-only and commits
 * by flipping a directory entry, so the framing is a trailer:
 * [payload][version][len][crc].
 * The commit leaves either the new file whole or the previous one; a version
 * other than BASIC_CKPT_VERSION is rejected, len must agree with the file
 * length, and the CRC guards against bit rot.
 */
#define BASIC_CKPT_FILE     "prog.ckpt"
#define BASIC_CKPT_TRAILER  12u        /* [version][len][crc], u32 each */
#define BASIC_CKPT_IMG_MAX  (BASIC_CKPT_PAYLOAD_MAX + BASIC_CKPT_TRAILER)

/* Both durable BASIC objects must fit the store together, the checkpoint at
 * twice its span because a replace writes the new run before the directory
 * entry flips.  The assert is a floor: other files share the store, and when
 * they exhaust it at run time a save fails with NOSPACE. */
_Static_assert(TIKU_TFS_SPAN_FOR(TIKU_BASIC_SAVE_BUF_BYTES)
                   + 2u * TIKU_TFS_SPAN_FOR(BASIC_CKPT_IMG_MAX)
                   <= TIKU_TFS_MIN_SLOTS,
               "BASIC's saved program + checkpoint cannot fit the /data store");
_Static_assert(TIKU_BASIC_SAVE_BUF_BYTES <= TIKU_TFS_FILE_MAX_GUARANTEED,
               "BASIC's saved program exceeds the largest possible store file");

static tiku_tfs_wr_t basic_ckpt_wr;    /* open while a save is in flight */

/** @brief The /data store, or NULL when none is mounted. */
static tiku_tfs_t *
basic_ckpt_fs(void)
{
    return tiku_vfs_tree_data_store();
}

/*
 * Streaming or staging.  Streaming the payload in bounded chunks keeps one
 * chunk of RAM; staging keeps a whole image.  RP2350 stages the image and
 * writes it in one call: its flash erases and reprograms every 4 KB sector a
 * write touches, so each chunked append would cost a sector erase.  Every
 * other region part streams.
 */
#if defined(PLATFORM_RP2350)
#define BASIC_CKPT_STREAMING  0
#else
#define BASIC_CKPT_STREAMING  1
#endif

#if BASIC_CKPT_STREAMING
/* Bounded staging: the RAM cost is one chunk, whatever the payload size. */
#define BASIC_CKPT_CHUNK  512u
static uint8_t basic_ckpt_chunk[BASIC_CKPT_CHUNK];
#else
/* Whole-image staging for the single-call write; BASIC_SCRATCH is plain .bss
 * on RP2350, rebuilt on every save. */
static BASIC_SCRATCH uint8_t
    basic_ckpt_scratch[BASIC_CKPT_RGN_HDR + BASIC_CKPT_PAYLOAD_MAX];
#endif

#else
/* Byte-writable slot: durable FRAM on MSP430, plain .bss on host (session-only,
 * as SAVE is there).  Not built on region parts. */
static BASIC_NVM_PERSISTENT uint8_t basic_ckpt_buf[TIKU_BASIC_CKPT_BYTES];
#define BASIC_CKPT_STREAMING  0
#endif

/*---------------------------------------------------------------------------*/
/* BYTE CURSORS                                                               */
/*---------------------------------------------------------------------------*/

/** Serialization cursor for basic_ckpt_write(). */
typedef struct {
    uint8_t *base;          /**< whole image, the chunk, or NULL to measure */
    size_t   pos;           /**< payload bytes written so far, logically    */
    size_t   cap;           /**< capacity of base[]                         */
    int      err;           /**< set on overflow or a refused write         */
#if BASIC_CKPT_STREAMING
    size_t   fill;          /**< bytes currently buffered in base[]         */
    size_t   limit;         /**< payload bound, BASIC_CKPT_PAYLOAD_MAX      */
    uint32_t crc;           /**< running CRC over the flushed bytes         */
#endif
} basic_ckpt_wr_t;
/** Deserialization cursor for basic_ckpt_read(). */
typedef struct { const uint8_t *base; size_t pos, len; int err; } basic_ckpt_rd_t;

#if BASIC_RECLAIM_ENABLE
/** Output window for tiku_basic_reclaim.inl: while active, ckpt_w() copies
 *  only payload bytes [start, start + size) into out and writes no NVM, so the
 *  state is serialized a chunk at a time without a whole-state buffer. */
static struct {
    int active;
    uint8_t *out;
    size_t start, size;
    uint32_t identity;
} basic_ckpt_window;
#endif

#if BASIC_CKPT_STREAMING
/* Defined after basic_crc32_step, which it folds each chunk into. */
static int ckpt_flush(basic_ckpt_wr_t *w);
#endif

/**
 * @brief Append @p n bytes of state to @p w (or only count or window them).
 *
 * Callers write strictly in sequence -- nothing seeks or back-patches -- so
 * the same calls serve a whole image, a streamed chunk and a reclaim window.
 */
static void
ckpt_w(basic_ckpt_wr_t *w, const void *src, size_t n)
{
#if BASIC_RECLAIM_ENABLE
    if (basic_ckpt_window.active) {
        size_t end, first, last;
        if (w->pos > BASIC_CKPT_PAYLOAD_MAX ||
            n > BASIC_CKPT_PAYLOAD_MAX - w->pos) { w->err = 1; return; }
        end = w->pos + n;
        first = w->pos > basic_ckpt_window.start ? w->pos : basic_ckpt_window.start;
        last = basic_ckpt_window.start + basic_ckpt_window.size;
        if (end < last) last = end;
        if (basic_ckpt_window.out && first < last)
            memcpy(basic_ckpt_window.out + first - basic_ckpt_window.start,
                   (const uint8_t *)src + first - w->pos, last - first);
        w->pos = end;
        return;
    }
#endif
#if BASIC_CKPT_STREAMING
    const uint8_t *p = (const uint8_t *)src;

    if (w->base == NULL) {                      /* measuring: count only */
        if (w->pos + n > w->limit) {
            w->err = 1;
        }
        w->pos += n;
        return;
    }
    while (n > 0u) {
        size_t k;

        if (w->err) {
            return;
        }
        if (w->fill == w->cap && ckpt_flush(w) != 0) {
            return;                             /* backend refused the chunk */
        }
        k = w->cap - w->fill;
        if (k > n) {
            k = n;
        }
        if (w->pos + k > w->limit) {
            w->err = 1;                         /* would overrun the slot */
            return;
        }
        memcpy(w->base + w->fill, p, k);
        w->fill += k;
        w->pos  += k;
        p       += k;
        n       -= k;
    }
#else
    if (w->err || w->pos + n > w->cap) { w->err = 1; return; }
    memcpy(w->base + w->pos, src, n);
    w->pos += n;
#endif
}

/** @brief Read @p n bytes; past the end, zero @p dst and set r->err. */
static void
ckpt_r(basic_ckpt_rd_t *r, void *dst, size_t n)
{
    if (r->err || r->pos + n > r->len) { r->err = 1; memset(dst, 0, n); return; }
    memcpy(dst, r->base + r->pos, n);
    r->pos += n;
}

/*---------------------------------------------------------------------------*/
/* PROGRAM IDENTITY                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Incremental CRC-32 (reflected, poly 0xEDB88320).
 *
 * Seed with 0xFFFFFFFF and XOR the result with 0xFFFFFFFF to finalize.  Backs
 * the program identity, the prog.ckpt CRC and the reclaim image CRC.
 */
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
 * @brief The version word stored beside a checkpoint's payload.
 *
 * BASIC_CKPT_VERSION folded with the limits and struct sizes that place the
 * payload's fields, so a checkpoint written by a build with other limits is
 * refused before any of it is read.
 */
static uint32_t
basic_ckpt_format(void)
{
    static const uint32_t shape[] = {
        BASIC_CKPT_VERSION, (uint32_t)sizeof(long), BASIC_VAR_TABLE_LEN,
        TIKU_BASIC_NAMEDVAR_LEN, TIKU_BASIC_NAMEDVAR_MAX,
        (uint32_t)sizeof(basic_for_frame_t),
        (uint32_t)sizeof(basic_loop_frame_t),
        TIKU_BASIC_STRVARS_ENABLE, TIKU_BASIC_STR_HEAP_BYTES,
        TIKU_BASIC_SUBS_ENABLE, TIKU_BASIC_DEFN_ENABLE,
        TIKU_BASIC_EVERY_MAX, TIKU_BASIC_EVERY_STMT_LEN,
        TIKU_BASIC_ONCHG_MAX, TIKU_BASIC_ARRAYS_ENABLE,
#if TIKU_BASIC_SUBS_ENABLE
        (uint32_t)sizeof(basic_frame_t),
#endif
#if TIKU_BASIC_DEFN_ENABLE
        (uint32_t)sizeof(basic_defn_t),
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
        (uint32_t)sizeof(((basic_onchg_t *)0)->path),
#endif
    };

    return basic_crc32_step(0xFFFFFFFFu, (const uint8_t *)shape,
                            sizeof shape) ^ 0xFFFFFFFFu;
}

#if BASIC_CKPT_STREAMING
/**
 * @brief Append the buffered chunk to the open prog.ckpt writer.
 *
 * Folds the chunk into the running CRC on the way out, so the checksum never
 * needs the whole payload resident.
 *
 * @return 0 on success, -1 if the store refused the write (w->err is set).
 */
static int
ckpt_flush(basic_ckpt_wr_t *w)
{
    if (w->fill == 0u) {
        return 0;
    }
    w->crc = basic_crc32_step(w->crc, w->base, w->fill);
    /* Append-only: the store's writer holds the position. */
    if (tiku_tfs_write_chunk(&basic_ckpt_wr, w->base, w->fill) != TFS_OK) {
        w->err = 1;
        return -1;
    }
    w->fill = 0;
    return 0;
}
#endif

/**
 * @brief CRC-32 fingerprint of the in-memory program (each line's number and
 *        stored text, in line order).
 *
 * The checkpoint stores the fingerprint of the program it was captured from,
 * and RESUME rejects it unless the loaded program matches: the saved PC and
 * stacks hold line numbers.  An empty program, or no line table, gives 0.
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
 * The order must mirror basic_ckpt_read().  The string block writes heap_pos
 * and the heap prefix before the strvar offsets, so the reader can validate
 * each offset against the restored heap length.
 */
static void
basic_ckpt_write(basic_ckpt_wr_t *w)
{
    uint16_t i;
    uint8_t  u8;
    uint32_t pid = (w->base != NULL) ? basic_prog_identity() : 0u;
#if BASIC_RECLAIM_ENABLE
    if (basic_ckpt_window.active) pid = basic_ckpt_window.identity;
#endif

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
    ckpt_w(w, basic_namedvar_const, (size_t)TIKU_BASIC_NAMEDVAR_MAX);

#if TIKU_BASIC_STRVARS_ENABLE
    ckpt_w(w, basic_namedstrvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);
    ckpt_w(w, &basic_str_heap_pos, sizeof(basic_str_heap_pos));
    ckpt_w(w, basic_str_heap, basic_str_heap_pos);          /* used prefix */
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
    /* SUB call frames and the LOCAL restore stack, so a program checkpointed
     * mid-CALL resumes inside the SUB with its LOCALs intact. */
    ckpt_w(w, &basic_call_sp, 1);
    for (i = 0; i < basic_call_sp; i++)
        ckpt_w(w, &basic_frames[i], sizeof(basic_frame_t));
    ckpt_w(w, &basic_scope_sp, 1);
    for (i = 0; i < basic_scope_sp; i++) {
        /* Serialize old_str as a heap offset (a raw pointer would not survive
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
    /* DEF FN table: active definitions only.  Lookup is by name, so the
     * restore packs them into slots 0..n-1. */
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
    /* EVERY slots: only interval_ms and stmt are saved.  The restore re-arms
     * start and interval_ticks from the current clock, which restarts at
     * boot. */
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
     * node cache, armed and pending flags are re-derived by the mode tick. */
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
     * present byte; an array that would overflow the budget writes present=0
     * and is not restored.  Numeric data is verbatim longs; string elements
     * are heap offsets, mirroring the strvar block. */
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

/** @brief Whether a text field read from a checkpoint ends inside its array. */
static int
ckpt_text_ok(const char *s, size_t cap)
{
    return memchr(s, '\0', cap) != NULL;
}

/**
 * @brief Refuse a restore that has begun, clearing what it wrote.
 *
 * Empties the control stacks, the variable namespace, the reactive tables and
 * the DATA cursor, so no value from the refused payload stays reachable.
 *
 * @return -1, for basic_ckpt_read() to return
 */
static int
ckpt_refuse(void)
{
    uint8_t k;

    gosub_sp = 0;
    for_sp   = 0;
    loop_sp  = 0;
#if TIKU_BASIC_SUBS_ENABLE
    basic_call_sp  = 0;
    basic_scope_sp = 0;
#endif
    basic_clear_vars();
#if TIKU_BASIC_EVERY_MAX > 0
    for (k = 0; k < TIKU_BASIC_EVERY_MAX; k++) basic_everys[k].active = 0;
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    for (k = 0; k < TIKU_BASIC_ONCHG_MAX; k++) basic_onchgs[k].active = 0;
#endif
    (void)k;
    basic_data_idx    = -1;
    basic_data_off    = 0;
    basic_err_handler = 0;
    basic_err_pc      = 0;
    basic_ckpt_armed  = 0;
    basic_pc = basic_pc_set = basic_trace = 0;
    basic_err = basic_erl = 0;
    basic_prng_state = 0;
    basic_prng_seeded = 0;
    return -1;
}

/**
 * @brief Restore the reified execution state from a serialized payload.
 *
 * Every depth, index, heap offset and text field is checked against the
 * compiled limits before use, and the payload must be consumed exactly; a
 * refused restore clears what it wrote (ckpt_refuse()).
 *
 * @return 0 when the restore completes, -1 if the payload is short or
 *         inconsistent.
 */
static int
basic_ckpt_read(const uint8_t *payload, size_t len)
{
    basic_ckpt_rd_t r = { payload, 0, len, 0 };
    uint16_t i;
    uint8_t  u8, sp;
    uint32_t pid;

    /* The program-identity check comes first, before any state is touched:
     * the checkpoint's PC and GOSUB/FOR line numbers belong to the program it
     * was captured from.  A different program (edited, or replaced by a later
     * SAVE) returns -1 with nothing written. */
    ckpt_r(&r, &pid, sizeof(pid));
    if (r.err || pid != basic_prog_identity()) {
        return -1;
    }

    ckpt_r(&r, &basic_pc, sizeof(basic_pc));
    ckpt_r(&r, &u8, 1); basic_pc_set = u8;
    ckpt_r(&r, &u8, 1); basic_trace  = u8;
    ckpt_r(&r, &u8, 1); basic_ckpt_armed = u8;

    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_GOSUB_DEPTH) return ckpt_refuse();
    gosub_sp = sp;
    for (i = 0; i < sp; i++) ckpt_r(&r, &gosub_stack[i], sizeof(uint16_t));
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_FOR_DEPTH) return ckpt_refuse();
    for_sp = sp;
    for (i = 0; i < sp; i++) {
        ckpt_r(&r, &for_stack[i], sizeof(basic_for_frame_t));
        if (for_stack[i].var_idx >= BASIC_VAR_TABLE_LEN) return ckpt_refuse();
    }
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_LOOP_DEPTH) return ckpt_refuse();
    loop_sp = sp;
    for (i = 0; i < sp; i++) ckpt_r(&r, &loop_stack[i], sizeof(basic_loop_frame_t));

    for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) ckpt_r(&r, &basic_vars[i], sizeof(long));
    ckpt_r(&r, basic_namedvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);
    ckpt_r(&r, basic_namedvar_const, (size_t)TIKU_BASIC_NAMEDVAR_MAX);
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        if (!ckpt_text_ok(basic_namedvar_names[i], TIKU_BASIC_NAMEDVAR_LEN)) {
            return ckpt_refuse();
        }
    }

#if TIKU_BASIC_STRVARS_ENABLE
    ckpt_r(&r, basic_namedstrvar_names,
           (size_t)TIKU_BASIC_NAMEDVAR_LEN * TIKU_BASIC_NAMEDVAR_MAX);
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        if (!ckpt_text_ok(basic_namedstrvar_names[i],
                          TIKU_BASIC_NAMEDVAR_LEN)) {
            return ckpt_refuse();
        }
    }
    {
        uint16_t hp;
        ckpt_r(&r, &hp, sizeof(hp));
        if (hp > TIKU_BASIC_STR_HEAP_BYTES) return ckpt_refuse();
        basic_str_heap_pos = hp;
        ckpt_r(&r, basic_str_heap, hp);
        /* Every heap string ends in a NUL, so the used prefix does too: an
         * offset below hp then always finds its terminator inside it. */
        if (hp > 0u && basic_str_heap[hp - 1u] != '\0') return ckpt_refuse();
        for (i = 0; i < BASIC_VAR_TABLE_LEN; i++) {
            uint16_t off;
            ckpt_r(&r, &off, sizeof(off));
            if (off == 0xFFFFu) {
                basic_strvars[i] = NULL;
            } else if (off < hp) {
                basic_strvars[i] = basic_str_heap + off;
            } else {
                return ckpt_refuse();       /* dangling offset -> reject */
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
    /* The DATA cursor indexes prog[] and that line's text. */
    if (basic_data_idx < -2 || basic_data_idx >= TIKU_BASIC_PROGRAM_LINES ||
        basic_data_off < 0 || basic_data_off >= TIKU_BASIC_LINE_MAX) {
        return ckpt_refuse();
    }

#if TIKU_BASIC_SUBS_ENABLE
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_CALL_DEPTH) return ckpt_refuse();
    basic_call_sp = sp;
    for (i = 0; i < sp; i++)
        ckpt_r(&r, &basic_frames[i], sizeof(basic_frame_t));
    ckpt_r(&r, &sp, 1);
    if (sp > TIKU_BASIC_SCOPE_MAX) return ckpt_refuse();
    basic_scope_sp = sp;
    for (i = 0; i < basic_call_sp; i++) {
        if (basic_frames[i].scope_base > basic_scope_sp ||
            (i && basic_frames[i].scope_base <
                  basic_frames[i - 1u].scope_base)) {
            return ckpt_refuse();
        }
    }
    for (i = 0; i < sp; i++) {
        basic_scope_t *s = &basic_scope[i];
        uint16_t soff;
        ckpt_r(&r, &s->idx,    sizeof(s->idx));
        ckpt_r(&r, &s->is_str, sizeof(s->is_str));
        ckpt_r(&r, &s->old,    sizeof(s->old));
        ckpt_r(&r, &soff,      sizeof(soff));
        /* ENDSUB writes the saved value back through idx and old_str. */
        if (s->idx >= BASIC_VAR_TABLE_LEN ||
            (soff != 0xFFFFu && soff >= basic_str_heap_pos)) {
            return ckpt_refuse();
        }
        s->old_str = (soff == 0xFFFFu) ? NULL : basic_str_heap + soff;
    }
    ckpt_r(&r, &basic_sub_result, sizeof(basic_sub_result));
#endif
#if TIKU_BASIC_DEFN_ENABLE
    {
        uint8_t nd, k;
        ckpt_r(&r, &nd, 1);
        if (nd > TIKU_BASIC_DEFN_MAX) return ckpt_refuse();
        for (k = 0; k < TIKU_BASIC_DEFN_MAX; k++) basic_defns[k].name[0] = '\0';
        for (k = 0; k < nd; k++) {
            basic_defn_t *d = &basic_defns[k];
            uint8_t       a;
            ckpt_r(&r, d, sizeof(basic_defn_t));
            /* A call binds arg_count arguments through arg_idx[]. */
            if (!ckpt_text_ok(d->name, sizeof d->name) ||
                !ckpt_text_ok(d->body, sizeof d->body) ||
                d->arg_count > TIKU_BASIC_DEFN_ARGS) {
                return ckpt_refuse();
            }
            for (a = 0; a < d->arg_count; a++) {
                if (d->arg_idx[a] >= 26u) return ckpt_refuse();
            }
        }
    }
#endif

#if TIKU_BASIC_EVERY_MAX > 0
    {
        uint8_t       n, k;
        unsigned long now = basic_ticks();
        ckpt_r(&r, &n, 1);
        if (n > TIKU_BASIC_EVERY_MAX) return ckpt_refuse();
        for (k = 0; k < n; k++) {
            long interval;
            ckpt_r(&r, &interval, sizeof(long));
            ckpt_r(&r, basic_everys[k].stmt, (size_t)TIKU_BASIC_EVERY_STMT_LEN);
            if (interval <= 0 ||
                !ckpt_text_ok(basic_everys[k].stmt,
                              TIKU_BASIC_EVERY_STMT_LEN)) {
                return ckpt_refuse();
            }
            basic_everys[k].interval_ms    = interval;
            basic_everys[k].interval_ticks =
                basic_ms_to_ticks((unsigned long)interval, 1);
            basic_everys[k].start          = now;          /* re-armed */
            basic_everys[k].active         = 1;
        }
    }
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    {
        uint8_t n, k;
        ckpt_r(&r, &n, 1);
        if (n > TIKU_BASIC_ONCHG_MAX) return ckpt_refuse();
        for (k = 0; k < n; k++) {
            ckpt_r(&r, basic_onchgs[k].path, sizeof(basic_onchgs[k].path));
            ckpt_r(&r, &basic_onchgs[k].last_value, sizeof(long));
            ckpt_r(&r, &basic_onchgs[k].handler_line, sizeof(uint16_t));
            ckpt_r(&r, &basic_onchgs[k].is_gosub, 1);
            if (!ckpt_text_ok(basic_onchgs[k].path,
                              sizeof(basic_onchgs[k].path))) {
                return ckpt_refuse();
            }
            basic_onchgs[k].active = 1;
#if TIKU_BASIC_ONCHG_EVENT
            /* Arena memory is not zeroed: reset the event runtime fields
             * explicitly; the mode tick re-arms on its next pass. */
            basic_onchgs[k].node    = NULL;
            basic_onchgs[k].armed   = 0;
            basic_onchgs[k].pending = 0;
#endif
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
                /* DIM's own limits; they also bound the zero fill a short
                 * payload makes ckpt_r() write into the elements. */
                if (d1 < 1u || d1 > TIKU_BASIC_ARRAY_MAX ||
                    d2 > TIKU_BASIC_ARRAY_MAX ||
                    total > (size_t)TIKU_BASIC_ARRAY_MAX) {
                    return ckpt_refuse();
                }
                a->dim1     = d1;
                a->dim2     = d2;
                a->is_string = (uint8_t)which;
#if TIKU_BASIC_STRVARS_ENABLE
                if (which) {
                    char **el = (char **)tiku_arena_alloc(&basic_arena,
                        (tiku_mem_arch_size_t)(sizeof(char *) * total));
                    size_t j;
                    if (el == NULL) return ckpt_refuse();
                    for (j = 0; j < total; j++) {
                        uint16_t off;
                        ckpt_r(&r, &off, sizeof(off));
                        if (off != 0xFFFFu && off >= basic_str_heap_pos) {
                            return ckpt_refuse();
                        }
                        el[j] = (off == 0xFFFFu) ? NULL : basic_str_heap + off;
                    }
                    a->data = el;
                } else
#endif
                {
                    long *el = (long *)tiku_arena_alloc(&basic_arena,
                        (tiku_mem_arch_size_t)(sizeof(long) * total));
                    if (el == NULL) return ckpt_refuse();
                    ckpt_r(&r, el, total * sizeof(long));
                    a->data = el;
                }
            }
        }
    }
#endif

    /* A payload with bytes left over was written to another layout. */
    return (r.err || r.pos != r.len) ? ckpt_refuse() : 0;
}

/*---------------------------------------------------------------------------*/
/* DURABLE CHECKPOINT                                                        */
/*---------------------------------------------------------------------------*/

#if BASIC_NVM_ON_REGION

/** @brief CRC-32 (reflected, poly 0xEDB88320) over @p n bytes at @p p. */
static uint32_t
basic_ckpt_crc32(const uint8_t *p, size_t n)
{
    return basic_crc32_step(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}

/**
 * @brief Delete prog.ckpt if it exists.
 *
 * Idempotent: with no checkpoint it writes nothing, so repeated calls cost no
 * NVM write (on flash even a one-word delete is a sector operation).
 */
static void
basic_ckpt_invalidate(void)
{
    tiku_tfs_t *fs = basic_ckpt_fs();
    size_t      len;

    if (fs == NULL) {
        return;
    }
    if (tiku_tfs_stat(fs, BASIC_CKPT_FILE, &len) != TFS_OK) {
        return;
    }
    (void)tiku_tfs_delete(fs, BASIC_CKPT_FILE);
}

/**
 * @brief Serialize the current state into prog.ckpt: the payload, then the
 *        [version][len][crc] trailer; the store commit makes it valid.
 * @return 0 on success, -1 on serialization failure or no usable store.
 */
static int
basic_ckpt_save(void)
{
    basic_ckpt_wr_t w;
    tiku_tfs_t     *fs = basic_ckpt_fs();
    uint32_t        tr[3];

    if (fs == NULL) {
        return -1;                     /* no store mounted this boot */
    }
    if (basic_ckpt_wr.active) {
        tiku_tfs_abort(&basic_ckpt_wr);   /* an abandoned save, released */
    }
    /* The file reserves only what this checkpoint writes, measured first. */
#if BASIC_CKPT_STREAMING
    memset(&w, 0, sizeof w);              /* base NULL: measure the payload */
    w.limit = BASIC_CKPT_PAYLOAD_MAX;
    basic_ckpt_write(&w);
    if (w.err ||
        tiku_tfs_open_w(fs, &basic_ckpt_wr, BASIC_CKPT_FILE,
                        w.pos + BASIC_CKPT_TRAILER) != TFS_OK) {
        return -1;
    }
    /* Payload in bounded chunks, then the trailer.  The store's directory
     * flip is the commit point, so a cut before it leaves the previous
     * checkpoint. */
    w.base  = basic_ckpt_chunk;
    w.cap   = sizeof basic_ckpt_chunk;
    w.pos   = 0;
    w.err   = 0;
    w.fill  = 0;
    w.limit = BASIC_CKPT_PAYLOAD_MAX;
    w.crc   = 0xFFFFFFFFu;                 /* CRC-32 init, finalized below */
    basic_ckpt_write(&w);
    if (w.err || ckpt_flush(&w) != 0) {
        tiku_tfs_abort(&basic_ckpt_wr);
        return -1;
    }
    tr[0] = basic_ckpt_format();
    tr[1] = (uint32_t)w.pos;
    tr[2] = w.crc ^ 0xFFFFFFFFu;           /* = basic_ckpt_crc32(payload) */
#else
    /* RP2350 flash: stage the whole image and hand it over in one
     * write_chunk; each chunked append would erase a 4 KB sector. */
    w.base = basic_ckpt_scratch;
    w.pos  = 0;
    w.cap  = sizeof basic_ckpt_scratch;
    w.err  = 0;
    basic_ckpt_write(&w);
    if (w.err ||
        tiku_tfs_open_w(fs, &basic_ckpt_wr, BASIC_CKPT_FILE,
                        w.pos + BASIC_CKPT_TRAILER) != TFS_OK) {
        return -1;
    }
    if (tiku_tfs_write_chunk(&basic_ckpt_wr, basic_ckpt_scratch,
                             w.pos) != TFS_OK) {
        tiku_tfs_abort(&basic_ckpt_wr);
        return -1;
    }
    tr[0] = basic_ckpt_format();
    tr[1] = (uint32_t)w.pos;
    tr[2] = basic_ckpt_crc32(basic_ckpt_scratch, w.pos);
#endif
    if (tiku_tfs_write_chunk(&basic_ckpt_wr, tr, sizeof tr) != TFS_OK ||
        tiku_tfs_commit(&basic_ckpt_wr) != TFS_OK) {
        tiku_tfs_abort(&basic_ckpt_wr);
        return -1;
    }
    return 0;
}

/**
 * @brief Restore state from the checkpoint file if it holds a valid one.
 *
 * Reads in place: the store maps the file, and a file's slots are contiguous,
 * so one pointer covers the whole image.
 *
 * @return 0 if a checkpoint was restored, -1 if none / stale / short / corrupt.
 */
static int
basic_ckpt_load(void)
{
    tiku_tfs_t    *fs = basic_ckpt_fs();
    const void    *p  = NULL;
    const uint8_t *img;
    size_t         n  = 0u;
    uint32_t       ver, len32, crc;

    if (fs == NULL ||
        tiku_tfs_map(fs, BASIC_CKPT_FILE, &p, &n) != TFS_OK ||
        n < BASIC_CKPT_TRAILER) {
        return -1;                                 /* no usable checkpoint */
    }
    img = (const uint8_t *)p;
    memcpy(&ver,   img + n - 12, 4);
    memcpy(&len32, img + n - 8,  4);
    memcpy(&crc,   img + n - 4,  4);
    if (ver != basic_ckpt_format()) return -1;     /* incompatible firmware */
    /* The payload length must agree with the file length: TFS already
     * guarantees the file is whole, so a mismatch is a foreign file under
     * this name, not a torn write. */
    if ((size_t)len32 + BASIC_CKPT_TRAILER != n ||
        len32 > BASIC_CKPT_PAYLOAD_MAX) {
        return -1;
    }
    if (basic_ckpt_crc32(img, (size_t)len32) != crc) {
        return -1;                                 /* bit rot */
    }
    return basic_ckpt_read(img, (size_t)len32);
}

#else  /* byte-writable slot: gate-last multi-store */

/**
 * @brief Invalidate the durable checkpoint (gate := 0).
 *
 * Idempotent: skips the MPU unlock and the store when the gate is already
 * clear, so repeated calls cost nothing.
 */
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
 * @brief Serialize the current state into the durable slot, gate last.
 * @return 0 on success, -1 if the payload overflowed the slot (the
 *         compile-time bound makes that unreachable).
 */
static int
basic_ckpt_save(void)
{
    basic_ckpt_wr_t w;
    uint16_t mpu;
    uint32_t z = 0u, ver = basic_ckpt_format(), magic = BASIC_CKPT_MAGIC;
    uint32_t len32;

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
        memcpy(basic_ckpt_buf, &magic, 4);         /* gate last: now valid */
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
    if (ver != basic_ckpt_format()) return -1;     /* incompatible firmware */
    if (len32 > TIKU_BASIC_CKPT_BYTES - BASIC_CKPT_HDR) return -1;
    return basic_ckpt_read(basic_ckpt_buf + BASIC_CKPT_HDR, (size_t)len32);
}

#endif /* BASIC_NVM_ON_REGION */

/*---------------------------------------------------------------------------*/
/* CHECKPOINT CADENCE                                                        */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_CKPT_INTERVAL_S > 0
/* Second of the most recent save (or arming), from the kernel clock. */
static unsigned long basic_ckpt_last_s;
#endif

/**
 * @brief 1 when a checkpoint is due at this yield boundary.
 *
 * Interval 0 checkpoints every batch; a nonzero TIKU_BASIC_CKPT_INTERVAL_S
 * allows at most one save per interval since PERSIST ON or the last save.
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
/* ARM AND DISARM                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Arm (1) or disarm (0) checkpointing, for PERSIST ON / OFF.
 *
 * Disarming drops any checkpoint; arming restarts the cadence interval, so
 * the first save lands one full interval after PERSIST ON.
 */
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

#else  /* !TIKU_BASIC_PERSIST_RUN_ENABLE */

/* PERSIST compiled out: save and load return -1, due returns 0, and
 * invalidate and mark do nothing.  basic_ckpt_arm() has no stub: its only
 * caller, exec_persist(), compiles the call out too. */
static int  basic_ckpt_save(void)       { return -1; }
static int  basic_ckpt_load(void)       { return -1; }
static void basic_ckpt_invalidate(void) { }
static int  basic_ckpt_due(void)        { return 0; }
static void basic_ckpt_mark(void)       { }

#endif /* TIKU_BASIC_PERSIST_RUN_ENABLE */
