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
 * The default unnamed `SAVE` and `LOAD` go through basic_save_buf
 * registered under BASIC_PERSIST_KEY ("prog") in the FRAM-backed
 * tiku_persist store.  Multi-slot named SAVE / LOAD live in
 * tiku_basic_named_slots.inl.
 *
 * Implementation is lazy: basic_persist_ensure() registers the save
 * buffer with the persist store on first use; subsequent SAVE / LOAD
 * calls just read / write through the bracketed MPU unlock.
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
    uint8_t *slot  = basic_region_slot();
    uint32_t magic = BASIC_REGION_MAGIC;
    uint32_t lenw  = (uint32_t)len;
    uint32_t zero  = 0u;

    if (slot == NULL || len + 8u > TIKU_NVM_RESERVED_BYTES) {
        return -1;
    }
    /* gate-last: clear magic, write text + len, stamp magic last, so a power
     * cut mid-save leaves the slot invalid rather than torn. */
    if (tiku_tier_nvm_write(slot, &zero, 4u) != TIKU_MEM_OK) {
        return -1;
    }
    if (len > 0u &&
        tiku_tier_nvm_write(slot + 8, text, (tiku_mem_arch_size_t)len)
            != TIKU_MEM_OK) {
        return -1;
    }
    if (tiku_tier_nvm_write(slot + 4, &lenw, 4u) != TIKU_MEM_OK) {
        return -1;
    }
    return (tiku_tier_nvm_write(slot, &magic, 4u) == TIKU_MEM_OK) ? 0 : -1;
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

/* Shared SAVE/LOAD serialization scratch. SAVE serializes the program into
 * it, LOAD deserializes out of it -- the two are single-threaded interpreter
 * commands that never run concurrently, so one buffer serves both. Sized +1
 * for LOAD's NUL terminator; SAVE uses the first TIKU_BASIC_SAVE_BUF_BYTES.
 * Folding the two per-function statics into one matters on RP2350, where
 * BASIC_SCRATCH is plain .bss (no .ssram section): each copy is
 * PROGRAM_LINES*(LINE_MAX+8) = ~76 KB on the 1024-line BIG tier, and the
 * second one tipped the all-features HTTPS+WiFi+BASIC build over the
 * 520 KB SRAM. */
static BASIC_SCRATCH char basic_persist_scratch[TIKU_BASIC_SAVE_BUF_BYTES + 1];

/**
 * @brief Serialise the in-memory program in ascending order and
 *        commit it to FRAM under BASIC_PERSIST_KEY.
 *
 * @return 0 on success, -1 on persist failure or buffer overflow.
 */
static int
basic_save_to_persist(void)
{
    /* Serialize ascending-ordered program lines (the shape LIST prints) into
     * the shared scratch, then commit it under the default slot via
     * basic_prog_store(). Capacity mirrors the old per-function buffer. */
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
    /* Deserializes from the shared scratch (see basic_persist_scratch
     * above); full capacity including the +1 for the NUL terminator. */
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
}
