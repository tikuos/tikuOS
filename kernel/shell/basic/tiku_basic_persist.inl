/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_persist.inl - default-slot SAVE and LOAD.
 *
 * Two backends: on region parts the program is the /data file prog.bas,
 * streamed out in bounded chunks and parsed in place on load, so neither
 * direction stages a whole program in RAM; MSP430 and host use tiku_persist.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_region.h"
#include "kernel/vfs/tree/tiku_vfs_tree_data.h"   /* the /data store itself */

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

/* Defined in tiku_basic_program.inl and tiku_basic_repl.inl, which
 * tiku_basic.c includes after this file. */
static void prog_clear(void);
static int  prog_next_index(uint16_t lineno);
static void process_line(const char *raw);

/*---------------------------------------------------------------------------*/
/* PROGRAM STORAGE                                                           */
/*---------------------------------------------------------------------------*/
/*
 * The default program slot.  On region parts (BASIC_NVM_ON_REGION) it is the
 * /data file BASIC_PROG_FILE, written through the store's streamed writer:
 * begin reserves a run, append adds bounded chunks, commit flips the directory
 * entry in one word, so a cut mid-SAVE leaves the previous program.  On MSP430
 * and host it is the BASIC_PERSIST_KEY entry of the tiku_persist store, held
 * in basic_save_buf.
 */

#if BASIC_NVM_ON_REGION
/*
 * A flat name, not "basic/prog": /data has a static node named "basic" (the
 * program bridge in tiku_vfs_tree_data.c, which reads through this file), and
 * a "basic/" prefix would make `ls /data` list a "basic/" folder beside it
 * while `ls /data/basic` resolves to the node and fails.
 */
#define BASIC_PROG_FILE  "prog.bas"

static tiku_tfs_wr_t basic_prog_wr;          /* open between begin and commit */

/** @brief The /data store, or NULL when none is mounted. */
static tiku_tfs_t *
basic_prog_fs(void)
{
    return tiku_vfs_tree_data_store();
}

/**
 * @brief Begin replacing the saved program, reserving @p max bytes.
 *
 * @p max is what this save writes, measured first, so a small SAVE needs only
 * that much contiguous room in the store.  An abandoned writer is released.
 *
 * @return 0, or -1 with no store mounted or no room
 */
static int
basic_prog_begin(size_t max)
{
    tiku_tfs_t *fs = basic_prog_fs();

    if (fs == NULL) {
        return -1;
    }
    if (basic_prog_wr.active) {
        tiku_tfs_abort(&basic_prog_wr);      /* an abandoned SAVE, released */
    }
    return (tiku_tfs_open_w(fs, &basic_prog_wr, BASIC_PROG_FILE, max) == TFS_OK)
           ? 0 : -1;
}

/** @brief Append @p n bytes to the open SAVE.  @return 0 or -1. */
static int
basic_prog_append(const void *p, size_t n)
{
    if (n == 0u) {
        return 0;
    }
    return (tiku_tfs_write_chunk(&basic_prog_wr, p, n) == TFS_OK) ? 0 : -1;
}

/**
 * @brief Commit the open SAVE; on failure release it, keeping the previous
 *        program.
 * @return 0 or -1
 */
static int
basic_prog_commit(void)
{
    if (tiku_tfs_commit(&basic_prog_wr) != TFS_OK) {
        tiku_tfs_abort(&basic_prog_wr);      /* previous program still stands */
        return -1;
    }
    return 0;
}

/** @brief Abandon the open SAVE; the previous program stands. */
static void
basic_prog_discard(void)
{
    tiku_tfs_abort(&basic_prog_wr);
}

/**
 * @brief Zero-copy view of the saved program text.
 *
 * The store maps the file in place, and a file's slots are contiguous, so one
 * pointer covers a program of any length.
 *
 * @param len_out  Receives the stored text length on success.
 * @return Pointer to the text inside the store, or NULL when none is saved.
 */
static const char *
basic_region_text(size_t *len_out)
{
    tiku_tfs_t *fs = basic_prog_fs();
    const void *p  = NULL;
    size_t      n  = 0u;

    if (fs == NULL || tiku_tfs_map(fs, BASIC_PROG_FILE, &p, &n) != TFS_OK ||
        n == 0u) {
        return NULL;
    }
    *len_out = n;
    return (const char *)p;
}
#else
/**
 * @brief Lazily register the save buffer with the persist store (MSP430/host).
 * @return 0 on success, -1 on persist-store failure.
 */
static int
basic_persist_ensure(void)
{
    uint16_t       mpu;
    tiku_mem_err_t rc1, rc2;

    if (basic_persist_ready) {
        return 0;
    }
    mpu = tiku_mpu_unlock_nvm();
    rc1 = tiku_persist_init(&basic_store);
    rc2 = tiku_persist_register(&basic_store, BASIC_PERSIST_KEY,
            basic_save_buf, TIKU_BASIC_SAVE_BUF_BYTES);
    tiku_mpu_lock_nvm(mpu);
    if (rc1 != TIKU_MEM_OK || rc2 != TIKU_MEM_OK) {
        return -1;
    }
    basic_persist_ready = 1;
    return 0;
}
#endif

/**
 * @brief Store @p len bytes of program text durably under the default slot.
 * @return 0 on success, -1 on failure.
 */
static int
basic_prog_store(const char *text, size_t len)
{
#if BASIC_NVM_ON_REGION
    if (len > TIKU_BASIC_SAVE_BUF_BYTES) {
        return -1;
    }
    /* Whole-image path for the /data/basic bridge, which supplies a complete
     * image; SAVE streams instead (basic_save_to_persist). */
    if (basic_prog_begin(len) != 0 ||
        basic_prog_append(text, len) != 0) {
        basic_prog_discard();
        return -1;
    }
    return basic_prog_commit();
#else
    uint16_t       mpu;
    tiku_mem_err_t rc;

    if (basic_persist_ensure() != 0) {
        return -1;
    }
    mpu = tiku_mpu_unlock_nvm();
    rc  = tiku_persist_write(&basic_store, BASIC_PERSIST_KEY,
            (const uint8_t *)text, (tiku_mem_arch_size_t)len);
    tiku_mpu_lock_nvm(mpu);
    return (rc == TIKU_MEM_OK) ? 0 : -1;
#endif
}

/**
 * @brief Fetch the saved program text into @p buf (@p out_len set on success).
 * @return 0 on success, -1 if no saved program / error.
 */
static int
basic_prog_fetch(char *buf, size_t max, size_t *out_len)
{
#if BASIC_NVM_ON_REGION
    const char *text;
    size_t      len = 0u;

    text = basic_region_text(&len);
    if (text == NULL || len > max) {
        return -1;
    }
    memcpy(buf, text, len);
    *out_len = len;
    return 0;
#else
    tiku_mem_arch_size_t got = 0;
    tiku_mem_err_t       rc;

    if (basic_persist_ensure() != 0) {
        return -1;
    }
    rc = tiku_persist_read(&basic_store, BASIC_PERSIST_KEY,
            (uint8_t *)buf, (tiku_mem_arch_size_t)max, &got);
    if (rc != TIKU_MEM_OK || got == 0u) {
        return -1;
    }
    *out_len = (size_t)got;
    return 0;
#endif
}

/*---------------------------------------------------------------------------*/
/* SAVE / LOAD                                                               */
/*---------------------------------------------------------------------------*/

/*
 * Serialization scratch.  On region parts it is a bounded chunk, not a
 * program image: LOAD reads the saved text in place in the memory-mapped
 * region, and SAVE serializes into the chunk and appends it to the file
 * whenever it cannot hold another maximum-length line.  4 KB is one RP2350
 * flash sector, the granule that part's backend erases and reprograms per
 * write, so a smaller chunk would multiply sector operations.  The size also
 * bounds the largest file IMPORT and the named SAVE/LOAD handle.
 *
 * MSP430 and host keep a whole-program buffer: their saved program lives in
 * the tiku_persist store, which has no in-place view to parse.
 */
#if BASIC_NVM_ON_REGION
#define BASIC_SCRATCH_BYTES  4096u
#else
#define BASIC_SCRATCH_BYTES  (TIKU_BASIC_SAVE_BUF_BYTES + 1u)
#endif
_Static_assert(BASIC_SCRATCH_BYTES >= (unsigned)TIKU_BASIC_LINE_MAX + 16u,
               "serialization scratch cannot hold one maximum-length line");
static BASIC_SCRATCH char basic_persist_scratch[BASIC_SCRATCH_BYTES];

#if BASIC_NVM_ON_REGION
/* One saved line at a time, for LOAD and named LOAD.  Not the scratch above:
 * process_line() is still reading the line when it can reach a command that
 * uses the scratch (IMPORT). */
static char basic_load_line[TIKU_BASIC_LINE_MAX + 16];
#endif

#if BASIC_NVM_ON_REGION
/**
 * @brief One pass over the program in LIST order through the bounded chunk.
 *
 * With @p emit the chunk goes to the open SAVE whenever it could not hold
 * another maximum-length line; without, the bytes are only counted.
 * @return 0, -1 on a write or format failure, -2 when the program would pass
 *         TIKU_BASIC_SAVE_BUF_BYTES.  @p total_out gets the bytes serialized.
 */
static int
basic_save_pass(int emit, size_t *total_out)
{
    char *const  chunk   = basic_persist_scratch;
    const size_t cap     = sizeof basic_persist_scratch;
    const size_t line_hw = (size_t)TIKU_BASIC_LINE_MAX + 16u;  /* worst line */
    size_t       fill    = 0;   /* serialized, not yet written to NVM */
    size_t       total   = 0;   /* already appended to the file       */
    uint16_t     cur     = 0;

    for (;;) {
        int idx = prog_next_index(cur);
        int n;

        if (idx < 0) {
            break;
        }
        if (cap - fill < line_hw) {              /* flush before it overflows */
            if (emit && basic_prog_append(chunk, fill) != 0) {
                return -1;
            }
            total += fill;
            fill   = 0;
        }
        if (total + fill + line_hw > TIKU_BASIC_SAVE_BUF_BYTES) {
            return -2;
        }
        /* Number, then the detokenized body: the saved format is plain
         * text, which LOAD crunches again. */
        n = snprintf(chunk + fill, cap - fill, "%u ",
                     (unsigned)prog[idx].number);
        if (n < 0 || (size_t)n >= cap - fill) {
            return -1;
        }
        fill += (size_t)n;
        n = basic_detok(chunk + fill, cap - fill, prog[idx].text);
        if (n < 0 || (size_t)n + 1u >= cap - fill) {
            return -1;
        }
        fill += (size_t)n;
        chunk[fill++] = '\n';
        if (prog[idx].number == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(prog[idx].number + 1);
    }
    if (fill > 0u && emit && basic_prog_append(chunk, fill) != 0) {
        return -1;
    }
    *total_out = total + fill;
    return 0;
}
#endif

/**
 * @brief Serialize the program in line order into the default slot: prog.bas
 *        on region parts, BASIC_PERSIST_KEY on MSP430/host.
 *
 * @return 0 on success, -1 on a write failure or a program too large.
 */
static int
basic_save_to_persist(void)
{
#if BASIC_NVM_ON_REGION
    /*
     * Streaming save, in LIST order, through the bounded chunk.  The text
     * goes into a fresh run and the directory flips to it at commit, so a cut
     * mid-SAVE leaves the previous program.  The pass runs twice: once to
     * measure, so the store reserves only what this save writes, then to
     * write.  TIKU_BASIC_SAVE_BUF_BYTES bounds the program, as the store-fit
     * assertion in tiku_basic_ckpt.inl assumes.
     */
    size_t total = 0;
    int    rc    = basic_save_pass(0, &total);

    if (rc == 0) {
        rc = (basic_prog_begin(total) == 0) ? basic_save_pass(1, &total) : -1;
        if (rc != 0) {
            basic_prog_discard();        /* previous saved program survives */
        }
    }
    if (rc == -2) {
        basic_report(TIKU_BASIC_ERR_IO, "save: program too large for slot");
        return -1;
    }
    if (rc != 0 || basic_prog_commit() != 0) {
        /* tiku_tfs_commit() leaves the writer active on its error returns, so
         * the reservation needs releasing here too. */
        basic_prog_discard();
        basic_report(TIKU_BASIC_ERR_IO, "save failed");
        return -1;
    }
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST "\n", (unsigned)total);
    return 0;
#else
    /* Serialize the program in LIST order into the whole-program scratch,
     * then commit it through basic_prog_store(): the tiku_persist store takes
     * the image in one write. */
    char *const  tmp     = basic_persist_scratch;
    const size_t tmp_cap = TIKU_BASIC_SAVE_BUF_BYTES;
    size_t      pos = 0;
    uint16_t    cur = 0;

    while (1) {
        int idx = prog_next_index(cur);
        int n;
        if (idx < 0) {
            break;
        }
        /* Number, then the detokenized body: the saved format is plain
         * text, which LOAD crunches again. */
        n = snprintf(tmp + pos, tmp_cap - pos, "%u ",
                     (unsigned)prog[idx].number);
        if (n < 0 || (size_t)n >= tmp_cap - pos) {
            basic_report(TIKU_BASIC_ERR_IO, "save: program too large for buffer");
            return -1;
        }
        pos += (size_t)n;
        n = basic_detok(tmp + pos, tmp_cap - pos, prog[idx].text);
        if (n < 0 || (size_t)n + 2u > tmp_cap - pos) {
            basic_report(TIKU_BASIC_ERR_IO, "save: program too large for buffer");
            return -1;
        }
        pos += (size_t)n;
        tmp[pos++] = '\n';
        tmp[pos]   = '\0';
        if (prog[idx].number == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(prog[idx].number + 1);
    }

    if (basic_prog_store(tmp, pos) != 0) {
        basic_report(TIKU_BASIC_ERR_IO, "save failed");
        return -1;
    }
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST "\n", (unsigned)pos);
    return 0;
#endif
}

/**
 * @brief Replay the saved program text through process_line() to rebuild
 *        the in-memory line table.
 *
 * @return 0 on success, -1 if nothing is saved or the read fails.
 */
static int
basic_load_from_persist(void)
{
#if BASIC_NVM_ON_REGION
    /* Read the program in place: the region is memory-mapped, so only the
     * line being dispatched is copied out, into basic_load_line. */
    size_t      len  = 0;
    const char *text = basic_region_text(&len);
    size_t      i, ls = 0;
    int         toolong = 0;

    if (text == NULL) {
        basic_report(TIKU_BASIC_ERR_IO, "load: no saved program");
        return -1;
    }

    /* Wipe the in-memory program and variables first, so the saved version
     * is what the user gets: not merged onto stale lines, and not tripping
     * "array already DIMmed" against a prior session's arrays. */
    prog_clear();
    basic_clear_vars();

    /* Walk the stored text a line at a time.  i == len feeds a synthetic
     * terminator so a final line without a newline is still dispatched; a
     * trailing newline just yields an empty line, which is skipped. */
    for (i = 0; i <= len; i++) {
        char c = (i < len) ? text[i] : '\n';

        if (c != '\n' && c != '\r') {
            continue;
        }
        if (i > ls) {
            size_t n = i - ls;
            if (n < sizeof basic_load_line) {
                memcpy(basic_load_line, text + ls, n);
                basic_load_line[n] = '\0';
                process_line(basic_load_line);
            } else {
                toolong = 1;      /* SAVE never writes one this long */
            }
        }
        ls = i + 1;
    }
    if (toolong) {
        basic_report(TIKU_BASIC_ERR_IO, "load: over-long line skipped");
    }

    SHELL_PRINTF(SH_GREEN "loaded %u bytes" SH_RST "\n", (unsigned)len);
    return 0;
#else
    /* Deserializes through the whole-program scratch: MSP430/host keep the
     * saved program in the tiku_persist store, which has no in-place view. */
    char *const  tmp     = basic_persist_scratch;
    const size_t tmp_cap = TIKU_BASIC_SAVE_BUF_BYTES + 1u;
    size_t      n_read = 0;
    char       *line_start;
    char       *p;

    if (basic_prog_fetch(tmp, tmp_cap - 1u, &n_read) != 0 ||
        n_read == 0u) {
        basic_report(TIKU_BASIC_ERR_IO, "load: no saved program");
        return -1;
    }
    tmp[n_read] = '\0';

    /* Wipe the in-memory program and variables first, so the saved version
     * is what the user gets: not merged onto stale lines, and not tripping
     * "array already DIMmed" against a prior session's arrays. */
    prog_clear();
    basic_clear_vars();

    /* Walk the buffer one line at a time, dispatching through
     * process_line.  Each line is a numbered statement, so each
     * call just stores it. */
    line_start = tmp;
    for (p = tmp; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            if (line_start != p) {
                process_line(line_start);
            }
            line_start = p + 1;
        }
    }
    if (line_start && *line_start) {
        process_line(line_start);
    }

    SHELL_PRINTF(SH_GREEN "loaded %u bytes" SH_RST "\n", (unsigned)n_read);
    return 0;
#endif
}
