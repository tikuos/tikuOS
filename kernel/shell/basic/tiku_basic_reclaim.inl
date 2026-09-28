/* BASIC callback owner: exact source plus lossless execution-state snapshot.
 * This is same-boot memory reconstruction, not automatic power-cut recovery.
 * SPDX-License-Identifier: Apache-2.0 */
#if BASIC_RECLAIM_ENABLE
#if !TIKU_TFS_HOLD_ENABLE
#error BASIC reconstruction requires TIKU_TFS_HOLD_ENABLE
#endif
#define BASIC_RECLAIM_FILE ".basic-reclaim"
#define BASIC_RECLAIM_CHUNK 512u
enum { BR_IDLE, BR_SOURCE, BR_STATE, BR_COMMITTED, BR_VERIFY, BR_LOAD, BR_READ };
static struct {
    tiku_tfs_t *fs;
    tiku_tfs_wr_t writer;
    tiku_tfs_hold_t hold;
    const uint8_t *image;
    size_t source_bytes, state_bytes, at, end;
    uint32_t header[8], crc, expected_crc;
    uint16_t line, lines, loaded;
    uint8_t stage, leased;
    const char *detail;
    uint8_t chunk[TIKU_BASIC_LINE_MAX + 6u > BASIC_RECLAIM_CHUNK ?
                  TIKU_BASIC_LINE_MAX + 6u : BASIC_RECLAIM_CHUNK];
} basic_reclaim;

/* One predicate serves both PREPARE and read-only owners telemetry. No
 * operation that silently loses state is treated as a successful checkpoint. */
static const char *basic_reclaim_eligibility(void)
{
    unsigned i;
    if (!basic_arena_ready || !basic_arena.active) return "no_arena";
    if (basic_reclaim_entered || basic_stmt_depth || basic_in_reactive ||
        (basic_running && !basic_run_shell_mode)) return "executing_statement";
    if (basic_wait_pending) return "waiting";
    if (basic_reclaim_external) return "external_resource";
#if TIKU_BASIC_DEBUG_ENABLE
    if (basic_debug_on || basic_debug_paused) return "debugger";
#endif
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++)
        if (basic_everys[i].active) return "active_timer";
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        if (basic_onchgs[i].active) return "active_watch";
#if TIKU_BASIC_ONCHG_EVENT
        if (basic_onchgs[i].armed || basic_onchgs[i].pending) return "active_watch";
#endif
    }
#if TIKU_BASIC_BIGBUF_COUNT > 0
    for (i = 0; i < TIKU_BASIC_BIGBUF_COUNT; i++)
        if (basic_biglen[i]) return "large_buffer";
#endif
#if TIKU_BASIC_ARRAYS_ENABLE
    {
        size_t remaining = BASIC_CKPT_ARR_BYTES;
        unsigned which;
        for (which = 0; which < (TIKU_BASIC_STRVARS_ENABLE ? 2u : 1u); which++)
            for (i = 0; i < 26; i++) {
                const basic_array_t *a = &basic_arrays[i];
                size_t count, unit;
#if TIKU_BASIC_STRVARS_ENABLE
                if (which) a = &basic_str_arrays[i];
#endif
                if (!a->data) continue;
                count = (size_t)a->dim1 * (a->dim2 ? a->dim2 : 1u);
                unit = which ? sizeof(uint16_t) : sizeof(long);
                if (!count || count > remaining / unit) return "array_budget";
                remaining -= count * unit;
            }
    }
#endif
    return "eligible";
}

static const char *basic_reclaim_describe(void *context)
{
    (void)context;
    /* Gating begins before the first callback and ends after the last. */
    if (!basic_reclaim_available())
        return basic_reclaim.detail ? basic_reclaim.detail : "preparing";
    if (basic_reclaim.detail && (!strcmp(basic_reclaim.detail, "cleanup_failed") ||
        !strcmp(basic_reclaim.detail, "save_failed") ||
        !strcmp(basic_reclaim.detail, "store_busy_or_full") || !strcmp(basic_reclaim.detail, "no_store")))
        return basic_reclaim.detail;
    return basic_reclaim_eligibility();
}

static size_t basic_reclaim_window(size_t at, uint8_t *out, size_t n)
{
    basic_ckpt_wr_t w = {0};
    basic_ckpt_window.active = 1;
    basic_ckpt_window.out = out;
    basic_ckpt_window.start = at;
    basic_ckpt_window.size = n;
    basic_ckpt_write(&w);
    basic_ckpt_window.active = 0;
    return w.err ? 0 : w.pos;
}

static int basic_reclaim_append(const void *data, size_t n)
{
    if (tiku_tfs_write_chunk(&basic_reclaim.writer, data, n) != TFS_OK) return -1;
    basic_reclaim.crc = basic_crc32_step(basic_reclaim.crc, data, n);
    return 0;
}

static void basic_reclaim_cleanup(void)
{
    int rc;
    const char *detail = basic_reclaim.detail;
    if (basic_reclaim.writer.active) tiku_tfs_abort(&basic_reclaim.writer);
    if (basic_reclaim.leased) {
        (void)tiku_tfs_release(&basic_reclaim.hold);
        basic_reclaim.leased = 0;
    }
    rc = basic_reclaim.fs ? tiku_tfs_delete(basic_reclaim.fs, BASIC_RECLAIM_FILE) : TFS_ERR_NOTFOUND;
    basic_reclaim.detail = rc == TFS_OK || rc == TFS_ERR_NOTFOUND ? "eligible" : "cleanup_failed";
    if (!strcmp(basic_reclaim.detail, "eligible") && detail &&
        (!strcmp(detail, "save_failed") || !strcmp(detail, "store_busy_or_full") || !strcmp(detail, "no_store")))
        basic_reclaim.detail = detail;
    basic_reclaim.stage = BR_IDLE;
    basic_reclaim.image = NULL;
}

static tiku_mem_owner_result_t basic_reclaim_step(void *context,
    tiku_mem_job_t job, tiku_mem_owner_phase_t phase)
{
    size_t n;
    (void)context; (void)job;
    if (phase == TIKU_MEM_OWNER_ABORT) {
        /* PREPARE has not reset, freed or changed any interpreter object. */
        basic_reclaim_cleanup();
        return TIKU_MEM_OWNER_DONE;
    }
    if (phase == TIKU_MEM_OWNER_PREPARE) {
        if (basic_reclaim.stage == BR_IDLE) {
            const char *why = basic_reclaim_eligibility();
            basic_reclaim.detail = why;
            if (strcmp(why, "eligible")) return TIKU_MEM_OWNER_BUSY;
            basic_reclaim.fs = basic_ckpt_fs();
            if (!basic_reclaim.fs) { basic_reclaim.detail = "no_store"; return TIKU_MEM_OWNER_BUSY; }
            basic_reclaim.lines = 0; basic_reclaim.source_bytes = 0;
            for (basic_reclaim.line = 0; basic_reclaim.line < TIKU_BASIC_PROGRAM_LINES; basic_reclaim.line++) {
                basic_line_t *l = &prog[basic_reclaim.line];
                if (!l->number) continue;
                for (n = 0; n < sizeof l->text && l->text[n]; n++) {}
                if (n == sizeof l->text) return TIKU_MEM_OWNER_BUSY;
                basic_reclaim.source_bytes += 6u + n + 1u;
                basic_reclaim.lines++;
            }
            basic_ckpt_window.identity = basic_prog_identity();
            basic_reclaim.state_bytes = basic_reclaim_window(0, NULL, 0);
            if (!basic_reclaim.state_bytes) return TIKU_MEM_OWNER_BUSY;
            basic_reclaim.header[0] = 0x42524331u; /* BRC1 */
            basic_reclaim.header[1] = BASIC_CKPT_VERSION;
            basic_reclaim.header[2] = TIKU_BASIC_PROGRAM_LINES;
            basic_reclaim.header[3] = TIKU_BASIC_LINE_MAX;
            basic_reclaim.header[4] = (uint32_t)basic_reclaim.source_bytes;
            basic_reclaim.header[5] = (uint32_t)basic_reclaim.state_bytes;
            basic_reclaim.header[6] = basic_reclaim.lines;
            basic_reclaim.header[7] = basic_ckpt_window.identity;
            n = sizeof basic_reclaim.header + basic_reclaim.source_bytes + basic_reclaim.state_bytes + 4u;
            if (tiku_tfs_open_w(basic_reclaim.fs, &basic_reclaim.writer, BASIC_RECLAIM_FILE, n) != TFS_OK) {
                basic_reclaim.detail = "store_busy_or_full";
                return TIKU_MEM_OWNER_BUSY;
            }
            basic_reclaim.crc = 0xFFFFFFFFu;
            if (basic_reclaim_append(basic_reclaim.header, sizeof basic_reclaim.header)) goto save_failed;
            basic_reclaim.line = 0; basic_reclaim.stage = BR_SOURCE;
            basic_reclaim.detail = "saving_source";
            return TIKU_MEM_OWNER_WAIT;
        }
        if (basic_reclaim.stage == BR_SOURCE) {
            while (basic_reclaim.line < TIKU_BASIC_PROGRAM_LINES) {
                uint16_t slot = basic_reclaim.line++, words[3];
                basic_line_t *l = &prog[slot];
                if (!l->number) continue;
                words[0] = slot; words[1] = l->number;
                words[2] = (uint16_t)(strlen(l->text) + 1u);
                memcpy(basic_reclaim.chunk, words, sizeof words);
                memcpy(basic_reclaim.chunk + sizeof words, l->text, words[2]);
                if (basic_reclaim_append(basic_reclaim.chunk, sizeof words + words[2])) goto save_failed;
                return TIKU_MEM_OWNER_WAIT;
            }
            basic_reclaim.at = 0; basic_reclaim.stage = BR_STATE;
            basic_reclaim.detail = "saving_state";
        }
        if (basic_reclaim.stage == BR_STATE) {
            n = basic_reclaim.state_bytes - basic_reclaim.at;
            if (n > BASIC_RECLAIM_CHUNK) n = BASIC_RECLAIM_CHUNK;
            if (n) {
                if (basic_reclaim_window(basic_reclaim.at, basic_reclaim.chunk, n) != basic_reclaim.state_bytes ||
                    basic_reclaim_append(basic_reclaim.chunk, n)) goto save_failed;
                basic_reclaim.at += n;
                return TIKU_MEM_OWNER_WAIT;
            }
            basic_reclaim.expected_crc = basic_reclaim.crc ^ 0xFFFFFFFFu;
            if (tiku_tfs_write_chunk(&basic_reclaim.writer, &basic_reclaim.expected_crc, 4) != TFS_OK ||
                tiku_tfs_commit(&basic_reclaim.writer) != TFS_OK ||
                tiku_tfs_hold(basic_reclaim.fs, &basic_reclaim.hold) != TFS_OK) goto save_failed;
            basic_reclaim.leased = 1;
            basic_reclaim.stage = BR_COMMITTED;
            basic_reclaim.detail = "snapshot_committed";
        }
        return TIKU_MEM_OWNER_DONE;
save_failed:
        if (basic_reclaim.writer.active) tiku_tfs_abort(&basic_reclaim.writer);
        basic_reclaim.detail = "save_failed";
        return TIKU_MEM_OWNER_BUSY;
    }

    /* RESTORE. Verify before touching any new backing. The expected CRC and
     * exact length are retained outside the arena; a replaced file is rejected
     * even if somebody supplied a self-consistent trailer. */
    if (basic_reclaim.stage == BR_COMMITTED) {
        const void *image;
        uint32_t crc;
        if (!basic_reclaim.leased ||
            tiku_tfs_map(basic_reclaim.fs, BASIC_RECLAIM_FILE, &image, &n) != TFS_OK ||
            n != sizeof basic_reclaim.header + basic_reclaim.source_bytes + basic_reclaim.state_bytes + 4u)
            goto restore_failed;
        basic_reclaim.image = image;
        memcpy(&crc, basic_reclaim.image + n - 4u, 4);
        if (crc != basic_reclaim.expected_crc ||
            memcmp(image, basic_reclaim.header, sizeof basic_reclaim.header)) goto restore_failed;
        basic_reclaim.end = n - 4u; basic_reclaim.at = 0;
        basic_reclaim.crc = 0xFFFFFFFFu;
        basic_reclaim.stage = BR_VERIFY;
        basic_reclaim.detail = "verifying";
    }
    if (basic_reclaim.stage == BR_VERIFY) {
        n = basic_reclaim.end - basic_reclaim.at;
        if (n > BASIC_RECLAIM_CHUNK) n = BASIC_RECLAIM_CHUNK;
        if (n) {
            basic_reclaim.crc = basic_crc32_step(basic_reclaim.crc,
                basic_reclaim.image + basic_reclaim.at, n);
            basic_reclaim.at += n;
            return TIKU_MEM_OWNER_WAIT;
        }
        if ((basic_reclaim.crc ^ 0xFFFFFFFFu) != basic_reclaim.expected_crc) goto restore_failed;
        basic_arena_ready = basic_arena.active;
        if (basic_alloc_state() != 0) goto restore_failed;
        basic_reclaim.at = sizeof basic_reclaim.header;
        basic_reclaim.loaded = 0; basic_reclaim.stage = BR_LOAD;
        basic_reclaim.detail = "restoring_source";
    }
    if (basic_reclaim.stage == BR_LOAD) {
        size_t end = sizeof basic_reclaim.header + basic_reclaim.source_bytes;
        if (basic_reclaim.loaded < basic_reclaim.lines) {
            uint16_t words[3];
            if (basic_reclaim.at > end || end - basic_reclaim.at < sizeof words) goto restore_failed;
            memcpy(words, basic_reclaim.image + basic_reclaim.at, sizeof words);
            basic_reclaim.at += sizeof words;
            if (words[0] >= TIKU_BASIC_PROGRAM_LINES || !words[1] || !words[2] ||
                words[2] > TIKU_BASIC_LINE_MAX || words[2] > end - basic_reclaim.at ||
                prog[words[0]].number || basic_reclaim.image[basic_reclaim.at + words[2] - 1]) goto restore_failed;
            prog[words[0]].number = words[1];
            memcpy(prog[words[0]].text, basic_reclaim.image + basic_reclaim.at, words[2]);
            basic_reclaim.at += words[2]; basic_reclaim.loaded++;
            return TIKU_MEM_OWNER_WAIT;
        }
        if (basic_reclaim.at != end) goto restore_failed;
        basic_reclaim.stage = BR_READ;
    }
    if (basic_reclaim.stage == BR_READ) {
        if (basic_ckpt_read(basic_reclaim.image + basic_reclaim.at, basic_reclaim.state_bytes)) goto restore_failed;
        basic_reclaim_cleanup();
        /* running/mode/stream and prompt state never changed. The shell's next
         * tick continues at the saved PC once the coordinator opens the gate. */
        return TIKU_MEM_OWNER_DONE;
    }
restore_failed:
    basic_reclaim.stage = BR_COMMITTED; /* retry verifies and rebuilds from scratch */
    basic_reclaim.detail = "restore_failed";
    return TIKU_MEM_OWNER_FAULT;
}

static int basic_reclaim_register(void)
{
    tiku_mem_owner_registration_t reg = {0};
    if (basic_reclaim_owner.slot_plus_one) return 0;
    reg.name = "BASIC"; reg.context = &basic_reclaim; reg.context_size = sizeof basic_reclaim;
    reg.step = basic_reclaim_step; reg.describe = basic_reclaim_describe;
    /* Bounded by the coordinator's half-range clock rule on 16-bit clocks. */
    reg.prepare_ticks = (tiku_clock_time_t)(TIKU_CLOCK_SECOND <= TIKU_CLOCK_MAX_INTERVAL / 30u ?
                                          30u * TIKU_CLOCK_SECOND : TIKU_CLOCK_MAX_INTERVAL);
    reg.recovery_ticks = reg.prepare_ticks;
    return tiku_mem_owner_register(&reg, &basic_reclaim_owner) == TIKU_MEM_OK ? 0 : -1;
}
#endif
