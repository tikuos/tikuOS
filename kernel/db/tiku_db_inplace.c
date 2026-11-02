/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_inplace.c - the extent on byte-addressable NVM: FRAM, MRAM, RRAM.
 *
 * Formats and mounts the header and the table entries, lays out a table's
 * block, and writes each record and commit entry in an order that leaves a
 * slot cut mid-write holding its old record, no record, or the new one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/db/tiku_db_store.h"

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL ACCESS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Backend write of @p n bytes at @p off; TIKU_DB_OK or TIKU_DB_ERR_IO. */
static int wr(tiku_db_t *db, uint32_t off, const void *p, uint32_t n)
{
    return db->be->write(db->be, off, p, n) == 0 ? TIKU_DB_OK : TIKU_DB_ERR_IO;
}

/** @brief Backend write of the 32-bit word @p v at @p off. */
static int wr32(tiku_db_t *db, uint32_t off, uint32_t v)
{
    return wr(db, off, &v, sizeof v);
}

/** @brief Store @p v little-endian at @p p. */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/** @brief Store @p v little-endian at @p p. */
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/** @brief Load a little-endian 32-bit word from @p p. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief FNV-1a over @p n bytes at @p p. */
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

/** @brief Byte offset of table entry @p i. */
static uint32_t tent_off(uint8_t i)
{
    return TDB_HDR_BYTES + (uint32_t)i * TDB_TENT_BYTES;
}

/** @brief The larger of two unsigned values. */
static uint32_t umax(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

/*---------------------------------------------------------------------------*/
/* LAYOUT                                                                    */
/*---------------------------------------------------------------------------*/

uint8_t tiku_db_eng_type_bytes(uint8_t type, uint8_t size)
{
    switch (type) {
    case TIKU_DB_I32:
    case TIKU_DB_U32:
    case TIKU_DB_Q3:
        return 4u;
    case TIKU_DB_T64:
        return 8u;
    case TIKU_DB_TEXT:
    case TIKU_DB_BYTES:
        return size;
    default:
        return 0u;
    }
}

int tiku_db_eng_layout(const tiku_db_media_t *media, const uint8_t *schema,
                       uint8_t ncols, uint16_t *rec_size, uint16_t *col_off)
{
    uint32_t unit = umax(media->unit, 4u);
    uint32_t off = (media->tear == TIKU_DB_TEAR_NONE) ? 4u : unit;
    uint32_t total;
    uint8_t i;

    for (i = 0; i < ncols; ++i) {
        const uint8_t *c = schema + (uint32_t)i * TDB_COL_BYTES;
        uint8_t bytes = tiku_db_eng_type_bytes(c[TDB_C_TYPE], c[TDB_C_SIZE]);
        if (bytes == 0u) {
            return TIKU_DB_ERR_TYPE;
        }
        col_off[i] = (uint16_t)off;
        off += bytes;
    }
    /* The echo closes the record.  Where a line cannot tear it may share the
     * last line with columns; elsewhere the last unit is the echo's alone,
     * with its complement after it where a line can tear. */
    if (media->tear == TIKU_DB_TEAR_NONE) {
        total = TDB_ALIGN(off + 4u, unit);
    } else {
        total = TDB_ALIGN(off, unit) + unit;
    }
    if (total > TIKU_DB_RECORD_MAX) {
        return TIKU_DB_ERR_INVAL;
    }
    *rec_size = (uint16_t)total;
    return TIKU_DB_OK;
}

/** @brief Offset of the ring inside a block: after the commit ring and schema. */
static uint32_t ring_rel(const tiku_db_t *db)
{
    return TDB_ALIGN((uint32_t)db->commits * db->stride + TDB_SCHEMA_BYTES,
                     db->media.unit);
}

uint32_t tiku_db_eng_block_len(const tiku_db_t *db, uint32_t capacity,
                               uint16_t rec_size)
{
    return TDB_ALIGN(ring_rel(db) + capacity * (uint32_t)rec_size,
                     TDB_BLOCK_ALIGN);
}

/*---------------------------------------------------------------------------*/
/* MOUNT                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Whether @p m describes a medium the engine can lay an extent on. */
static int media_ok(const tiku_db_media_t *m)
{
    uint16_t l = m->unit;
    return l >= 2u && l <= 64u && (l & (l - 1u)) == 0u &&
           (m->tear == TIKU_DB_TEAR_NONE || m->tear == TIKU_DB_TEAR_BYTE ||
            (m->tear == TIKU_DB_TEAR_LINE && l >= 8u));
}

/** @brief Fill @p db's medium-derived fields from @p be and @p m. */
static int attach(tiku_db_t *db, tiku_nvm_backend_t *be,
                  const tiku_db_media_t *m)
{
    if (db == NULL || be == NULL || be->base == NULL || be->write == NULL ||
        m == NULL || !media_ok(m) || be->size < TDB_DYN_OFF + 64u) {
        return TIKU_DB_ERR_INVAL;
    }
#if SIZE_MAX > 0xFFFFFF00u
    if (be->size > (size_t)0xFFFFFF00u) {
        return TIKU_DB_ERR_INVAL;
    }
#endif
    db->mounted = 0;
    db->be = be;
    db->media = *m;
    db->extent = (uint32_t)be->size;
    db->unit = (uint16_t)umax(m->unit, 4u);
    db->stride = (uint16_t)umax(m->unit, TDB_CENT_BYTES);
    db->identity = 0;
    memset(db->t, 0, sizeof db->t);
    return TIKU_DB_OK;
}

/**
 * @brief Derive table @p idx's SRAM state from its entry, schema and rings.
 *
 * @return TIKU_DB_OK, or TIKU_DB_ERR_CORRUPT when the entry, its schema or
 *         its block do not agree with each other or the extent
 */
static int load_table(tiku_db_t *db, uint8_t idx)
{
    tiku_db_tstate_t *t = &db->t[idx];
    uint32_t e = tent_off(idx);
    uint32_t cap, block, len, slot, j;
    uint16_t rec_size;
    uint64_t ring_end;
    uint8_t i, have;

    memset(t, 0, sizeof *t);
    if (tdb_rd32(db, e + TDB_T_GATE) != TDB_TABLE_LIVE) {
        return TIKU_DB_OK;
    }
    if (fnv(db->be->base + e + 4u, TDB_TENT_BYTES - 8u) !=
        tdb_rd32(db, e + TDB_T_CHECK)) {
        return TIKU_DB_ERR_CORRUPT;
    }
    t->ncols = db->be->base[e + TDB_T_NCOLS];
    t->policy = db->be->base[e + TDB_T_POLICY];
    rec_size = tdb_rd16(db, e + TDB_T_RECSIZE);
    cap = tdb_rd32(db, e + TDB_T_CAP);
    block = tdb_rd32(db, e + TDB_T_BLOCK);
    len = tdb_rd32(db, e + TDB_T_BLOCKLEN);
    t->rate = tdb_rd32(db, e + TDB_T_RATE);
    t->life = tdb_rd32(db, e + TDB_T_LIFE);
    if (t->ncols == 0u || t->ncols > TIKU_DB_COLS_MAX ||
        t->policy > TIKU_DB_KEEP || cap == 0u ||
        db->be->base[e + TDB_T_NAME] == 0u ||
        memchr(db->be->base + e + TDB_T_NAME, 0, TIKU_DB_NAME_MAX) == NULL ||
        block < TDB_DYN_OFF || (block & (TDB_BLOCK_ALIGN - 1u)) != 0u ||
        len < ring_rel(db) || block > db->extent || len > db->extent - block) {
        return TIKU_DB_ERR_CORRUPT;
    }
    t->capacity = cap;
    t->block_off = block;
    t->block_len = len;
    t->commit_off = block;
    t->schema_off = block + (uint32_t)db->commits * db->stride;
    t->ring_off = block + ring_rel(db);
    /* The entry's record size must be the one its schema lays out. */
    {
        uint16_t derived;
        if (tiku_db_eng_layout(&db->media, db->be->base + t->schema_off,
                               t->ncols, &derived, t->col_off) != TIKU_DB_OK ||
            derived != rec_size) {
            return TIKU_DB_ERR_CORRUPT;
        }
    }
    t->rec_size = rec_size;
    ring_end = (uint64_t)t->ring_off + (uint64_t)cap * rec_size;
    if (ring_end > (uint64_t)block + len) {
        return TIKU_DB_ERR_CORRUPT;
    }
    for (i = 0; i < t->ncols; ++i) {
        const uint8_t *c = db->be->base + t->schema_off + (uint32_t)i * TDB_COL_BYTES;
        if (c[TDB_C_NAME] == 0u ||
            memchr(c + TDB_C_NAME, 0, TIKU_DB_COL_NAME_MAX) == NULL) {
            return TIKU_DB_ERR_CORRUPT;
        }
    }

    /* The highest sequence number handed out is the largest echo that sits
     * in its own slot; a slot whose gate agrees with its echo is live. */
    for (slot = 0; slot < cap; ++slot) {
        uint32_t off = t->ring_off + slot * rec_size;
        uint32_t gate = tdb_rd32(db, off);
        uint32_t echo = tdb_echo(db, t, off);
        if (echo != 0u && echo % cap == slot) {
            t->max_seq = umax(t->max_seq, echo);
            if (gate == echo) {
                t->count++;
            }
        }
    }

    /* The commit ring: the valid entry with the greatest counter. */
    have = 0;
    for (j = 0; j < db->commits; ++j) {
        const uint8_t *p = db->be->base + t->commit_off + j * db->stride;
        uint32_t n = get32(p + TDB_E_N);
        uint32_t kind = get32(p + TDB_E_KIND);
        if (n == 0u || (kind != TDB_OPEN && kind != TDB_CLOSED) ||
            fnv(p, TDB_E_CHECK) != get32(p + TDB_E_CHECK)) {
            continue;
        }
        if (!have || (int32_t)(n - t->commit_n) > 0) {
            have = 1;
            t->commit_n = n;
            t->base = get32(p + TDB_E_BASE);
            t->open = (kind == TDB_OPEN);
        }
    }
    t->recover = t->open;
    t->live = 1;
    return TIKU_DB_OK;
}

int tiku_db_mount(tiku_db_t *db, tiku_nvm_backend_t *be,
                  const tiku_db_media_t *m)
{
    int rc = attach(db, be, m);
    uint8_t i, j;

    if (rc != TIKU_DB_OK) {
        return rc;
    }
    if (tdb_rd32(db, TDB_H_MAGIC) != TDB_MAGIC) {
        return TIKU_DB_ERR_NOSTORE;
    }
    if (tdb_rd16(db, TDB_H_VERSION) != TDB_VERSION ||
        fnv(db->be->base + 4u, TDB_HDR_BYTES - 8u) != tdb_rd32(db, TDB_H_CHECK)) {
        return TIKU_DB_ERR_CORRUPT;
    }
    if (tdb_rd16(db, TDB_H_LINE) != m->unit ||
        db->be->base[TDB_H_TEAR] != m->tear ||
        db->be->base[TDB_H_TABLES] != TIKU_DB_TABLES_MAX ||
        db->be->base[TDB_H_COMMITS] == 0u ||
        tdb_rd32(db, TDB_H_EXTENT) != db->extent) {
        return TIKU_DB_ERR_MEDIA;
    }
    db->commits = db->be->base[TDB_H_COMMITS];
    db->identity = tdb_rd32(db, TDB_H_IDENTITY);
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        rc = load_table(db, i);
        if (rc != TIKU_DB_OK) {
            return rc;
        }
    }
    /* Two live tables that claim one byte fail the mount. */
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        for (j = (uint8_t)(i + 1u); j < TIKU_DB_TABLES_MAX; ++j) {
            const tiku_db_tstate_t *a = &db->t[i], *b = &db->t[j];
            if (a->live && b->live &&
                a->block_off < b->block_off + b->block_len &&
                b->block_off < a->block_off + a->block_len) {
                return TIKU_DB_ERR_CORRUPT;
            }
        }
    }
    db->mounted = 1;
    return TIKU_DB_OK;
}

int tiku_db_probe(const tiku_nvm_backend_t *be, const tiku_db_media_t *m,
                  uint8_t *tables)
{
    const uint8_t *b;
    uint32_t v;
    uint16_t v16;
    uint8_t i, n = 0;

    if (be == NULL || be->base == NULL || m == NULL || tables == NULL ||
        !media_ok(m) || be->size < TDB_DYN_OFF + 64u) {
        return TIKU_DB_ERR_INVAL;
    }
    *tables = 0;
    b = be->base;
    memcpy(&v, b + TDB_H_MAGIC, sizeof v);
    if (v != TDB_MAGIC) {
        return TIKU_DB_ERR_NOSTORE;
    }
    memcpy(&v16, b + TDB_H_VERSION, sizeof v16);
    memcpy(&v, b + TDB_H_CHECK, sizeof v);
    if (v16 != TDB_VERSION || fnv(b + 4u, TDB_HDR_BYTES - 8u) != v) {
        return TIKU_DB_ERR_CORRUPT;
    }
    memcpy(&v16, b + TDB_H_LINE, sizeof v16);
    memcpy(&v, b + TDB_H_EXTENT, sizeof v);
    if (v16 != m->unit || b[TDB_H_TEAR] != m->tear ||
        b[TDB_H_TABLES] != TIKU_DB_TABLES_MAX || v != (uint32_t)be->size) {
        return TIKU_DB_ERR_MEDIA;
    }
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        const uint8_t *e = b + tent_off(i);
        memcpy(&v, e + TDB_T_GATE, sizeof v);
        if (v == TDB_TABLE_LIVE) {
            memcpy(&v, e + TDB_T_CHECK, sizeof v);
            if (fnv(e + 4u, TDB_TENT_BYTES - 8u) != v) {
                return TIKU_DB_ERR_CORRUPT;
            }
            n++;
        }
    }
    *tables = n;
    return TIKU_DB_OK;
}

/*---------------------------------------------------------------------------*/
/* FORMAT                                                                    */
/*---------------------------------------------------------------------------*/

int tiku_db_format(tiku_db_t *db, tiku_nvm_backend_t *be,
                   const tiku_db_media_t *m, uint32_t identity)
{
    uint8_t hdr[TDB_HDR_BYTES];
    uint8_t zero[TDB_TENT_BYTES];
    uint8_t i;
    int rc = attach(db, be, m);

    if (rc != TIKU_DB_OK) {
        return rc;
    }
    /* A store already here is retired before its tables are cleared, so a
     * cut mid-format never leaves it with some of them. */
    if (tdb_rd32(db, TDB_H_MAGIC) == TDB_MAGIC && wr32(db, TDB_H_MAGIC, 0u)) {
        return TIKU_DB_ERR_IO;
    }
    memset(zero, 0, sizeof zero);
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        uint32_t e = tent_off(i);
        if (memcmp(db->be->base + e, zero, sizeof zero) != 0 &&
            wr(db, e, zero, sizeof zero)) {
            return TIKU_DB_ERR_IO;
        }
    }
    memset(hdr, 0, sizeof hdr);
    put16(hdr + TDB_H_VERSION, TDB_VERSION);
    put16(hdr + TDB_H_LINE, m->unit);
    hdr[TDB_H_TEAR] = m->tear;
    hdr[TDB_H_TABLES] = TIKU_DB_TABLES_MAX;
    hdr[TDB_H_COMMITS] = TIKU_DB_COMMIT_LINES;
    put32(hdr + TDB_H_IDENTITY, identity);
    put32(hdr + TDB_H_EXTENT, db->extent);
    put32(hdr + TDB_H_CHECK, fnv(hdr + 4u, TDB_HDR_BYTES - 8u));
    if (wr(db, 4u, hdr + 4u, TDB_HDR_BYTES - 4u) ||
        wr32(db, TDB_H_MAGIC, TDB_MAGIC)) {
        return TIKU_DB_ERR_IO;
    }
    return tiku_db_mount(db, be, m);
}

/*---------------------------------------------------------------------------*/
/* TABLES                                                                    */
/*---------------------------------------------------------------------------*/

int tiku_db_eng_create(tiku_db_t *db, uint8_t idx, const char *name,
                       const uint8_t *schema, uint8_t ncols,
                       const tiku_db_spec_t *spec, uint16_t rec_size,
                       uint32_t block_off, uint32_t block_len)
{
    uint8_t zero[TDB_BLOCK_ALIGN];
    uint8_t ent[TDB_TENT_BYTES];
    uint32_t off;

    /* Zero the block, 64 bytes at a time, skipping what is zero already. */
    memset(zero, 0, sizeof zero);
    for (off = 0; off < block_len; off += TDB_BLOCK_ALIGN) {
        if (memcmp(db->be->base + block_off + off, zero, sizeof zero) != 0 &&
            wr(db, block_off + off, zero, sizeof zero)) {
            return TIKU_DB_ERR_IO;
        }
    }
    if (wr(db, block_off + (uint32_t)db->commits * db->stride, schema,
           (uint32_t)ncols * TDB_COL_BYTES)) {
        return TIKU_DB_ERR_IO;
    }
    memset(ent, 0, sizeof ent);
    memcpy(ent + TDB_T_NAME, name, strlen(name));
    ent[TDB_T_NCOLS] = ncols;
    ent[TDB_T_POLICY] = spec->policy;
    put16(ent + TDB_T_RECSIZE, rec_size);
    put32(ent + TDB_T_CAP, spec->capacity);
    put32(ent + TDB_T_BLOCK, block_off);
    put32(ent + TDB_T_BLOCKLEN, block_len);
    put32(ent + TDB_T_RATE, spec->rate);
    put32(ent + TDB_T_LIFE, spec->life);
    put32(ent + TDB_T_CHECK, fnv(ent + 4u, TDB_TENT_BYTES - 8u));
    if (wr(db, tent_off(idx) + 4u, ent + 4u, TDB_TENT_BYTES - 4u) ||
        wr32(db, tent_off(idx) + TDB_T_GATE, TDB_TABLE_LIVE)) {
        return TIKU_DB_ERR_IO;
    }
    return load_table(db, idx);
}

int tiku_db_eng_drop(tiku_db_t *db, uint8_t idx)
{
    if (wr32(db, tent_off(idx) + TDB_T_GATE, 0u)) {
        return TIKU_DB_ERR_IO;
    }
    memset(&db->t[idx], 0, sizeof db->t[idx]);
    return TIKU_DB_OK;
}

/*---------------------------------------------------------------------------*/
/* RECORDS                                                                   */
/*---------------------------------------------------------------------------*/

int tiku_db_eng_put(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t seq)
{
    uint32_t off = tdb_slot_off(t, seq);
    uint32_t n = t->rec_size;
    uint32_t u = db->unit;

    /* The echo's unit first: once it holds the new sequence number the old
     * record is unreadable.  Then the columns.  The gate's unit last: once it
     * holds the new sequence number the new record is readable.  A record of
     * one unit is one write. */
    if (wr(db, off + n - u, db->rec + n - u, u)) {
        return TIKU_DB_ERR_IO;
    }
    if (n > 2u * u && wr(db, off + u, db->rec + u, n - 2u * u)) {
        return TIKU_DB_ERR_IO;
    }
    if (n > u && wr(db, off, db->rec, u)) {
        return TIKU_DB_ERR_IO;
    }
    return TIKU_DB_OK;
}

int tiku_db_eng_kill(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t seq)
{
    return wr32(db, tdb_slot_off(t, seq), 0u);
}

int tiku_db_eng_entry(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t kind,
                      uint32_t base)
{
    uint8_t body[TDB_E_CHECK];
    uint32_t n = t->commit_n + 1u;
    uint32_t off;

    if (n == 0u) {
        n = 1u;
    }
    off = t->commit_off + (n % db->commits) * db->stride;
    put32(body + TDB_E_N, n);
    put32(body + TDB_E_BASE, base);
    put32(body + TDB_E_KIND, kind);
    /* The check lands after the body it covers; a cut between the two, or
     * inside either, leaves an entry the mount scan passes over. */
    if (wr(db, off, body, sizeof body) ||
        wr32(db, off + TDB_E_CHECK, fnv(body, sizeof body))) {
        return TIKU_DB_ERR_IO;
    }
    t->commit_n = n;
    return TIKU_DB_OK;
}
