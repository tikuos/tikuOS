/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_persist.inl - default-slot SAVE / LOAD via NVM store.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * The default unnamed `SAVE` and `LOAD` have two backends, selected by
 * BASIC_NVM_ON_REGION:
 *
 *   region-backed parts (Nordic RRAM, Ambiq MRAM, RP2350 flash)
 *       The program lives at the BASE of the carved NVM region's reserved tail,
 *       as [magic][len][text].  SAVE streams into it through a bounded 4 KB
 *       chunk; LOAD parses the text IN PLACE, since the region is memory-mapped.
 *       Neither direction stages a whole program in RAM.
 *   MSP430 / host
 *       basic_save_buf registered under BASIC_PERSIST_KEY ("prog") in the
 *       FRAM-backed tiku_persist store, whole-buffer in both directions --
 *       there is no mapped region to parse in place.  Registration is lazy
 *       (basic_persist_ensure() on first use); reads and writes then go through
 *       the bracketed MPU unlock.
 *
 * Named SAVE / LOAD live in tiku_basic_named_slots.inl, and are /data files on
 * the region-backed parts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_region.h"

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

/* Program-table helpers and the REPL line dispatcher are defined
 * further down in the orchestrator. */
static void prog_clear(void);
static int  prog_next_index(uint16_t lineno);
static void process_line(const char *raw);

/*---------------------------------------------------------------------------*/
/* PROGRAM-BLOB STORAGE (durable backend for the default "prog" slot)        */
/*---------------------------------------------------------------------------*/
/*
 * SAVE / LOAD and the /data/basic bridge all go through basic_prog_store() /
 * basic_prog_fetch() so they stay consistent. On the region-backed parts
 * (BASIC_NVM_ON_REGION: Ambiq MRAM, RP2350 flash) the program lives at the
 * BASE of the carved NVM region's reserved tail ([magic][len][text], gate-last
 * through the backend's program op); elsewhere it rides the tiku_persist store
 * over the BASIC_NVM_PERSISTENT save buffer.  The F1 run-state checkpoint is
 * the tail's second tenant, at the TOP -- see tiku_basic_ckpt.inl, which also
 * asserts the two slots fit the tail together.
 */

#if BASIC_NVM_ON_REGION
#define BASIC_REGION_MAGIC  0x42415350u   /* 'BASP' */
_Static_assert(TIKU_BASIC_SAVE_BUF_BYTES + 8u <= TIKU_NVM_RESERVED_BYTES,
               "BASIC save buffer larger than the reserved NVM region tail");

/* Base of the reserved NVM-region tail (program slot at offset 0), or NULL. */
static uint8_t *
basic_region_slot(void)
{
    const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();
    if (rgn == NULL || rgn->base == NULL ||
        rgn->size < TIKU_NVM_RESERVED_BYTES) {
        return NULL;
    }
    return rgn->base + (rgn->size - TIKU_NVM_RESERVED_BYTES);
}

/*
 * Slot primitives.  The slot is [magic][len][text...] and the write order is
 * always gate-clear -> text -> len -> magic, so a power cut at any point leaves
 * the slot INVALID rather than half-updated.  Splitting that discipline out of
 * basic_prog_store() lets SAVE append text in bounded chunks (see
 * basic_save_to_persist) while both paths keep exactly one commit rule.
 *
 * Note what is NOT bounded here: the durable slot still holds a whole program
 * (TIKU_BASIC_SAVE_BUF_BYTES, asserted against the tail above).  Only the RAM
 * used to get bytes into it is bounded.
 */
static int
basic_region_gate_clear(uint8_t *slot)
{
    uint32_t zero = 0u;
    return (tiku_tier_nvm_write(slot, &zero, 4u) == TIKU_MEM_OK) ? 0 : -1;
}

static int
basic_region_append(uint8_t *slot, size_t off, const void *p, size_t n)
{
    if (n == 0u) {
        return 0;
    }
    return (tiku_tier_nvm_write(slot + 8 + off, p,
                                (tiku_mem_arch_size_t)n) == TIKU_MEM_OK)
           ? 0 : -1;
}

static int
basic_region_commit(uint8_t *slot, size_t len)
{
    uint32_t magic = BASIC_REGION_MAGIC;
    uint32_t lenw  = (uint32_t)len;

    if (tiku_tier_nvm_write(slot + 4, &lenw, 4u) != TIKU_MEM_OK) {
        return -1;
    }
    return (tiku_tier_nvm_write(slot, &magic, 4u) == TIKU_MEM_OK) ? 0 : -1;
}

/**
 * @brief Zero-copy view of the saved program text inside the region.
 *
 * The region is memory-mapped and read in place, so LOAD does not need to copy
 * the program into RAM before parsing it -- which is the whole reason the
 * serialization scratch could shrink.  Returns NULL when no valid saved
 * program is present.
 *
 * @param len_out  Receives the stored text length on success.
 * @return Pointer to the text inside the region, or NULL.
 */
static const char *
basic_region_text(size_t *len_out)
{
    const uint8_t *slot = basic_region_slot();
    uint32_t magic, lenw;

    if (slot == NULL) {
        return NULL;
    }
    memcpy(&magic, slot, 4u);
    memcpy(&lenw,  slot + 4, 4u);
    if (magic != BASIC_REGION_MAGIC || lenw == 0u ||
        (size_t)lenw + 8u > TIKU_NVM_RESERVED_BYTES) {
        return NULL;
    }
    *len_out = (size_t)lenw;
    return (const char *)(slot + 8);
}
#else
/**
 * @brief Lazily register the save buffer with the persist store (non-Ambiq).
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
    uint8_t *slot = basic_region_slot();

    /* Bound by the PROGRAM slot's capacity, not the whole tail: the tail's top
     * belongs to the F1 checkpoint (tiku_basic_ckpt.inl). */
    if (slot == NULL || len > TIKU_BASIC_SAVE_BUF_BYTES) {
        return -1;
    }
    /* Whole-blob path, kept for the /data/basic VFS bridge, which hands us a
     * complete image.  SAVE itself streams -- see basic_save_to_persist. */
    if (basic_region_gate_clear(slot) != 0 ||
        basic_region_append(slot, 0u, text, len) != 0) {
        return -1;
    }
    return basic_region_commit(slot, len);
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
    const uint8_t *slot = basic_region_slot();
    uint32_t magic, lenw;

    if (slot == NULL) {
        return -1;
    }
    memcpy(&magic, slot, 4u);
    memcpy(&lenw,  slot + 4, 4u);
    if (magic != BASIC_REGION_MAGIC || lenw == 0u || (size_t)lenw > max) {
        return -1;
    }
    memcpy(buf, slot + 8, (size_t)lenw);
    *out_len = (size_t)lenw;
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
 * SERIALIZATION SCRATCH -- BOUNDED, not program-sized.
 *
 * This used to be a whole worst-case program image, PROGRAM_LINES*(LINE_MAX+8)
 * bytes of always-resident RAM: 155,649 B on the nRF54LM20 (63% of its primary
 * SRAM bank) and 258,401 B on the Apollo510, reserved at link time whether or
 * not SAVE or LOAD was ever used, and duplicating the arena's own prog[] line
 * table almost exactly -- 2.07 bytes of live SRAM per byte of program capacity.
 *
 * On the region-backed parts neither direction actually needs it:
 *   - LOAD reads the program in place.  The region is memory-mapped, so the
 *     saved text is already addressable; there is nothing to copy in.
 *   - SAVE streams.  It serializes into this fixed chunk and flushes to the
 *     slot whenever the chunk cannot hold another maximum-length line, so RAM
 *     is independent of program size.
 *
 * 4 KB is chosen, not arbitrary: it is one RP2350 flash sector (the granule
 * region_write() erases and reprograms per call, so a smaller chunk would
 * multiply sector operations) and simultaneously one /data file, which is the
 * largest thing IMPORT can be handed.  Sizes below one max-length line are a
 * build error.
 *
 * MSP430 and host keep the whole-program buffer: MSP430's saved program lives
 * in the tiku_persist store rather than a mapped region, so there is no
 * in-place text to parse, and at 96 lines the buffer is 14.6 KB in upper FRAM
 * -- not SRAM, and not the problem this solves.
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
/* One line at a time, for LOAD.  Deliberately NOT the scratch above: a saved
 * line is dispatched through process_line() while we still hold a pointer into
 * the buffer, and process_line() can reach commands that use the scratch
 * themselves (IMPORT).  160 bytes buys that aliasing hazard away. */
static char basic_load_line[TIKU_BASIC_LINE_MAX + 16];
#endif

/**
 * @brief Serialise the in-memory program in ascending order and
 *        commit it to FRAM under BASIC_PERSIST_KEY.
 *
 * @return 0 on success, -1 on persist failure or buffer overflow.
 */
static int
basic_save_to_persist(void)
{
#if BASIC_NVM_ON_REGION
    /*
     * Streaming save.  Serialize ascending-ordered lines (the shape LIST
     * prints) into the bounded chunk and flush to the slot whenever the chunk
     * could not hold another maximum-length line, so RAM cost is independent
     * of program length.
     *
     * The commit rule is unchanged: the magic is cleared before the first byte
     * of text and stamped only after the last.  A cut in between therefore
     * leaves the slot INVALID -- no saved program -- rather than a torn one
     * that would load as garbage.  (The old whole-blob path behaved the same
     * way; preserving the PREVIOUS save across a failed one would need a
     * shadow slot, and the reserved tail has room for only one.)
     *
     * The size bound is TIKU_BASIC_SAVE_BUF_BYTES, not the whole tail: the
     * tail's top is the F1 checkpoint slot (tiku_basic_ckpt.inl), and with no
     * program-sized staging buffer left to bound it implicitly, this check is
     * the only thing keeping the text out of it.
     */
    uint8_t     *slot    = basic_region_slot();
    char *const  chunk   = basic_persist_scratch;
    const size_t cap     = sizeof basic_persist_scratch;
    const size_t line_hw = (size_t)TIKU_BASIC_LINE_MAX + 16u;  /* worst line */
    size_t       fill    = 0;   /* serialized, not yet written to NVM */
    size_t       total   = 0;   /* already written to the slot        */
    uint16_t     cur     = 0;
    int          err     = 0;

    if (slot == NULL || basic_region_gate_clear(slot) != 0) {
        basic_report(TIKU_BASIC_ERR_IO, "save failed");
        return -1;
    }
    for (;;) {
        int idx = prog_next_index(cur);
        int n;

        if (idx < 0) {
            break;
        }
        if (cap - fill < line_hw) {              /* flush before it cannot fit */
            if (basic_region_append(slot, total, chunk, fill) != 0) {
                err = 1;
                break;
            }
            total += fill;
            fill   = 0;
        }
        if (total + fill + line_hw > TIKU_BASIC_SAVE_BUF_BYTES) {
            basic_report(TIKU_BASIC_ERR_IO, "save: program too large for slot");
            return -1;
        }
        /* Number, then the DETOKENIZED body (A2): the on-media format stays
         * plain text, so pre-A2 saves load unchanged and LOAD re-crunches. */
        n = snprintf(chunk + fill, cap - fill, "%u ",
                     (unsigned)prog[idx].number);
        if (n < 0 || (size_t)n >= cap - fill) {
            err = 1;
            break;
        }
        fill += (size_t)n;
        n = basic_detok(chunk + fill, cap - fill, prog[idx].text);
        if (n < 0 || (size_t)n + 1u >= cap - fill) {
            err = 1;
            break;
        }
        fill += (size_t)n;
        chunk[fill++] = '\n';
        if (prog[idx].number == 0xFFFFu) {
            break;
        }
        cur = (uint16_t)(prog[idx].number + 1);
    }
    if (!err && fill > 0u) {
        if (basic_region_append(slot, total, chunk, fill) != 0) {
            err = 1;
        } else {
            total += fill;
        }
    }
    if (err || basic_region_commit(slot, total) != 0) {
        basic_report(TIKU_BASIC_ERR_IO, "save failed");
        return -1;
    }
    SHELL_PRINTF(SH_GREEN "saved %u bytes" SH_RST "\n", (unsigned)total);
    return 0;
#else
    /* Serialize ascending-ordered program lines (the shape LIST prints) into
     * the whole-program scratch, then commit it under the default slot via
     * basic_prog_store().  MSP430/host: the saved program lives in the
     * tiku_persist store, not a mapped region, so there is nothing to stream
     * into and the buffer is the transfer medium. */
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
        /* Number, then the DETOKENIZED body (A2): the on-media format stays
         * plain text, so pre-A2 saves load unchanged and LOAD re-crunches. */
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
 * @brief Read the FRAM-backed program text and replay it through
 *        process_line() to repopulate the in-memory line table.
 *
 * @return 0 on success, -1 if no saved program exists or persist
 *         read fails.
 */
static int
basic_load_from_persist(void)
{
#if BASIC_NVM_ON_REGION
    /*
     * Read the program in place.  The region is memory-mapped, so the saved
     * text is already addressable and nothing has to be copied into RAM first
     * -- only the single line being dispatched is copied out, into a 160-byte
     * buffer.  This is what let the serialization scratch stop being
     * program-sized.
     */
    size_t      len  = 0;
    const char *text = basic_region_text(&len);
    size_t      i, ls = 0;
    int         toolong = 0;

    if (text == NULL) {
        basic_report(TIKU_BASIC_ERR_IO, "load: no saved program");
        return -1;
    }

    /* Wipe the in-memory program AND variables before loading, so the saved
     * version is what the user actually gets: not merged onto stale lines, and
     * not tripping "array already DIMmed" against a prior session's arrays. */
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
                toolong = 1;      /* only reachable on a corrupt slot */
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

    /* Wipe the in-memory program AND variables before loading, so the saved
     * version is what the user actually gets: not merged onto stale lines, and
     * not tripping "array already DIMmed" against a prior session's arrays. */
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
