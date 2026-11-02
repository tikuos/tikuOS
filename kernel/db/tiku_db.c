/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db.c - tables, records, transactions and cursors over the engine.
 *
 * Checks every argument against the schema, builds records, keeps the
 * transaction rule (records above an open entry's base are hidden) and
 * walks rings in sequence order.  The medium is tiku_db_inplace.c's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>

#include "kernel/db/tiku_db_store.h"

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Store @p v little-endian at @p p. */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/** @brief Load a little-endian 32-bit word from @p p. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Load a little-endian 64-bit word from @p p. */
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

/** @brief Whether @p c may appear in a table or column name. */
static int name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/** @brief Whether @p s is 1 to @p max - 1 name characters. */
static int name_ok(const char *s, size_t max)
{
    size_t n;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (n = 0; s[n] != '\0'; ++n) {
        if (n + 1u >= max || !name_char(s[n])) {
            return 0;
        }
    }
    return 1;
}

/** @brief The live table @p table of a mounted @p db, or NULL. */
static tiku_db_tstate_t *table_of(tiku_db_t *db, int table)
{
    if (db == NULL || !db->mounted || table < 0 || table >= TIKU_DB_TABLES_MAX ||
        !db->t[table].live) {
        return NULL;
    }
    return &db->t[table];
}

/** @brief Whether record @p seq of @p t is outside an open transaction. */
static int visible(const tiku_db_tstate_t *t, uint32_t seq)
{
    return !(t->open && seq > t->base);
}

/** @brief Numeric value of column @p col in record @p rec, widened. */
static int64_t col_num(const tiku_db_t *db, const tiku_db_tstate_t *t,
                       const uint8_t *rec, uint8_t col)
{
    const uint8_t *p = rec + t->col_off[col];
    switch (tdb_col_type(db, t, col)) {
    case TIKU_DB_I32:
    case TIKU_DB_Q3:
        return (int32_t)get32(p);
    case TIKU_DB_U32:
        return (int64_t)get32(p);
    case TIKU_DB_T64:
        return (int64_t)get64(p);
    default:
        return 0;
    }
}

/** @brief Whether column @p col holds a number. */
static int col_numeric(const tiku_db_t *db, const tiku_db_tstate_t *t,
                       uint8_t col)
{
    uint8_t type = tdb_col_type(db, t, col);
    return type != TIKU_DB_TEXT && type != TIKU_DB_BYTES;
}

/** @brief Whether a three-way comparison result @p cmp satisfies @p op. */
static int compare(int cmp, uint8_t op)
{
    switch (op) {
    case TIKU_DB_EQ: return cmp == 0;
    case TIKU_DB_NE: return cmp != 0;
    case TIKU_DB_LT: return cmp < 0;
    case TIKU_DB_LE: return cmp <= 0;
    case TIKU_DB_GT: return cmp > 0;
    default:         return cmp >= 0;
    }
}

/**
 * @brief Whether record @p rec passes filter @p f of table @p t.
 *
 * A TEXT or BYTES value is compared as the column would store it: zero
 * padded to the column's size.
 */
static int filter_pass(const tiku_db_t *db, const tiku_db_tstate_t *t,
                       const uint8_t *rec, const tiku_db_filter_t *f)
{
    if (col_numeric(db, t, f->col)) {
        int64_t v = col_num(db, t, rec, f->col);
        int cmp = (v < f->val.n) ? -1 : (v > f->val.n);
        return compare(cmp, f->op);
    } else {
        const uint8_t *p = rec + t->col_off[f->col];
        uint16_t size = tdb_col_size(db, t, f->col);
        uint16_t i;
        int equal = 1;
        for (i = 0; i < size && equal; ++i) {
            uint8_t want = i < f->val.len ? ((const uint8_t *)f->val.p)[i] : 0u;
            equal = (p[i] == want);
        }
        return f->op == TIKU_DB_EQ ? equal : !equal;
    }
}

/*---------------------------------------------------------------------------*/
/* EXTENT                                                                    */
/*---------------------------------------------------------------------------*/

uint32_t tiku_db_identity(const tiku_db_t *db)
{
    return (db != NULL && db->mounted) ? db->identity : 0u;
}

const char *tiku_db_err_name(int rc)
{
    switch (rc) {
    case TIKU_DB_OK:           return "ok";
    case TIKU_DB_ERR_INVAL:    return "invalid";
    case TIKU_DB_ERR_IO:       return "io";
    case TIKU_DB_ERR_NOSTORE:  return "no-store";
    case TIKU_DB_ERR_CORRUPT:  return "corrupt";
    case TIKU_DB_ERR_NOSPACE:  return "no-space";
    case TIKU_DB_ERR_EXISTS:   return "exists";
    case TIKU_DB_ERR_NOTFOUND: return "not-found";
    case TIKU_DB_ERR_FULL:     return "full";
    case TIKU_DB_ERR_BUSY:     return "busy";
    case TIKU_DB_ERR_LIFE:     return "outlived";
    case TIKU_DB_ERR_MEDIA:    return "other-medium";
    case TIKU_DB_ERR_TYPE:     return "type";
    default:                   return "unknown";
    }
}

/*---------------------------------------------------------------------------*/
/* TABLES                                                                    */
/*---------------------------------------------------------------------------*/

uint32_t tiku_db_capacity_for_life(const tiku_db_media_t *media,
                                   const tiku_db_spec_t *spec)
{
    uint64_t inserts;
    if (media == NULL || spec == NULL || media->endurance == 0u ||
        spec->rate == 0u || spec->life == 0u) {
        return 0u;
    }
    inserts = (uint64_t)spec->rate * 24u * spec->life;
    inserts = (inserts + media->endurance - 1u) / media->endurance;
    return inserts > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)inserts;
}

/**
 * @brief The lowest 64-aligned offset of a free run of @p len bytes.
 *
 * Live blocks are visited in offset order; the gap before each and the tail
 * of the extent are candidates.
 */
static int find_block(const tiku_db_t *db, uint32_t len, uint32_t *off)
{
    uint32_t cursor = TDB_DYN_OFF;
    uint8_t done[TIKU_DB_TABLES_MAX];
    uint8_t i, n;

    memset(done, 0, sizeof done);
    for (n = 0; n < TIKU_DB_TABLES_MAX; ++n) {
        int best = -1;
        for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
            if (db->t[i].live && !done[i] &&
                (best < 0 || db->t[i].block_off < db->t[best].block_off)) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        done[best] = 1;
        if (db->t[best].block_off - cursor >= len) {
            *off = cursor;
            return TIKU_DB_OK;
        }
        cursor = db->t[best].block_off + db->t[best].block_len;
    }
    if (db->extent - cursor >= len) {
        *off = cursor;
        return TIKU_DB_OK;
    }
    return TIKU_DB_ERR_NOSPACE;
}

int tiku_db_create(tiku_db_t *db, const char *name,
                   const tiku_db_coldef_t *cols, uint8_t ncols,
                   const tiku_db_spec_t *spec, uint32_t *needed)
{
    uint8_t schema[TDB_SCHEMA_BYTES];
    uint16_t col_off[TIKU_DB_COLS_MAX];
    uint16_t rec_size;
    uint32_t block_len, block_off, want;
    uint64_t ring_bytes;
    uint8_t i, j;
    int idx = -1;
    int rc;

    if (needed != NULL) {
        *needed = 0;
    }
    if (db == NULL || !db->mounted || cols == NULL || spec == NULL ||
        ncols == 0u || ncols > TIKU_DB_COLS_MAX || spec->capacity == 0u ||
        spec->policy > TIKU_DB_KEEP || !name_ok(name, TIKU_DB_NAME_MAX)) {
        return TIKU_DB_ERR_INVAL;
    }
    memset(schema, 0, sizeof schema);
    for (i = 0; i < ncols; ++i) {
        uint8_t *c = schema + (uint32_t)i * TDB_COL_BYTES;
        uint8_t bytes = tiku_db_eng_type_bytes(cols[i].type, cols[i].size);
        if (bytes == 0u) {
            return TIKU_DB_ERR_TYPE;
        }
        if (!name_ok(cols[i].name, TIKU_DB_COL_NAME_MAX)) {
            return TIKU_DB_ERR_INVAL;
        }
        for (j = 0; j < i; ++j) {
            if (strcmp(cols[i].name, cols[j].name) == 0) {
                return TIKU_DB_ERR_INVAL;
            }
        }
        c[TDB_C_TYPE] = cols[i].type;
        c[TDB_C_SIZE] = bytes;
        memcpy(c + TDB_C_NAME, cols[i].name, strlen(cols[i].name));
    }
    rc = tiku_db_eng_layout(&db->media, schema, ncols, &rec_size, col_off);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        if (db->t[i].live) {
            if (strcmp((const char *)db->be->base + TDB_HDR_BYTES +
                       (uint32_t)i * TDB_TENT_BYTES + TDB_T_NAME, name) == 0) {
                return TIKU_DB_ERR_EXISTS;
            }
        } else if (idx < 0) {
            idx = i;
        }
    }
    if (idx < 0) {
        return TIKU_DB_ERR_NOSPACE;
    }
    want = tiku_db_capacity_for_life(&db->media, spec);
    if (want > spec->capacity) {
        if (needed != NULL) {
            *needed = want;
        }
        return TIKU_DB_ERR_LIFE;
    }
    ring_bytes = (uint64_t)spec->capacity * rec_size;
    if (ring_bytes > (uint64_t)db->extent) {
        return TIKU_DB_ERR_NOSPACE;
    }
    block_len = tiku_db_eng_block_len(db, spec->capacity, rec_size);
    if ((uint64_t)block_len < ring_bytes) {
        return TIKU_DB_ERR_NOSPACE;
    }
    rc = find_block(db, block_len, &block_off);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    rc = tiku_db_eng_create(db, (uint8_t)idx, name, schema, ncols, spec,
                            rec_size, block_off, block_len);
    return rc == TIKU_DB_OK ? idx : rc;
}

int tiku_db_drop(tiku_db_t *db, int table)
{
    if (table_of(db, table) == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    return tiku_db_eng_drop(db, (uint8_t)table);
}

int tiku_db_open(const tiku_db_t *db, const char *name)
{
    uint8_t i;
    if (db == NULL || !db->mounted || name == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        if (db->t[i].live &&
            strcmp((const char *)db->be->base + TDB_HDR_BYTES +
                   (uint32_t)i * TDB_TENT_BYTES + TDB_T_NAME, name) == 0) {
            return i;
        }
    }
    return TIKU_DB_ERR_NOTFOUND;
}

int tiku_db_table_live(const tiku_db_t *db, int table)
{
    return db != NULL && db->mounted && table >= 0 &&
           table < TIKU_DB_TABLES_MAX && db->t[table].live;
}

int tiku_db_info(const tiku_db_t *db, int table, tiku_db_info_t *info)
{
    const tiku_db_tstate_t *t;
    if (info == NULL || !tiku_db_table_live(db, table)) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    t = &db->t[table];
    memcpy(info->name, db->be->base + TDB_HDR_BYTES +
           (uint32_t)table * TDB_TENT_BYTES + TDB_T_NAME, TIKU_DB_NAME_MAX);
    info->ncols = t->ncols;
    info->policy = t->policy;
    info->rec_size = t->rec_size;
    info->capacity = t->capacity;
    info->rate = t->rate;
    info->life = t->life;
    return TIKU_DB_OK;
}

int tiku_db_column(const tiku_db_t *db, int table, uint8_t col,
                   tiku_db_col_t *out)
{
    const tiku_db_tstate_t *t;
    const uint8_t *c;
    if (out == NULL || !tiku_db_table_live(db, table) ||
        col >= db->t[table].ncols) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    t = &db->t[table];
    c = db->be->base + t->schema_off + (uint32_t)col * TDB_COL_BYTES;
    memcpy(out->name, c + TDB_C_NAME, TIKU_DB_COL_NAME_MAX);
    out->type = c[TDB_C_TYPE];
    out->size = c[TDB_C_SIZE];
    out->offset = t->col_off[col];
    return TIKU_DB_OK;
}

int tiku_db_stat(const tiku_db_t *db, int table, tiku_db_stat_t *st)
{
    const tiku_db_tstate_t *t;
    if (st == NULL || !tiku_db_table_live(db, table)) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    t = &db->t[table];
    memset(st, 0, sizeof *st);
    st->capacity = t->capacity;
    st->count = t->count;
    st->max_seq = t->max_seq;
    st->laps = (t->max_seq + t->capacity - 1u) / t->capacity;
    st->rec_size = t->rec_size;
    st->lines = (uint16_t)(t->rec_size / db->media.unit);
    st->txn_open = t->open;
    st->recovering = t->recover;
    st->rate = t->rate;
    st->life = t->life;
    st->life_days = 0xFFFFFFFFu;
    if (db->media.endurance != 0u && t->rate != 0u) {
        uint64_t days = 0;
        if (st->laps < db->media.endurance) {
            days = (uint64_t)(db->media.endurance - st->laps) * t->capacity /
                   ((uint64_t)t->rate * 24u);
        }
        st->life_days = days > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)days;
    }
    return TIKU_DB_OK;
}

/*---------------------------------------------------------------------------*/
/* TRANSACTIONS                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delete every live record above the open entry's base, then close it.
 *
 * Each delete is idempotent, so a cut anywhere here is finished by the next
 * call; the entry's close is the one write that ends the transaction.
 */
static int finish_abort(tiku_db_t *db, tiku_db_tstate_t *t)
{
    uint32_t lo = tdb_window_lo(t);
    uint32_t seq;
    int rc;

    if (t->base + 1u > lo) {
        lo = t->base + 1u;
    }
    for (seq = lo; seq != 0u && seq <= t->max_seq; ++seq) {
        if (tdb_live(db, t, seq)) {
            rc = tiku_db_eng_kill(db, t, seq);
            if (rc != TIKU_DB_OK) {
                return rc;
            }
            t->count--;
        }
    }
    rc = tiku_db_eng_entry(db, t, TDB_CLOSED, t->base);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    t->open = 0;
    t->recover = 0;
    return TIKU_DB_OK;
}

int tiku_db_begin(tiku_db_t *db, int table)
{
    tiku_db_tstate_t *t = table_of(db, table);
    int rc;

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (t->open) {
        if (!t->recover) {
            return TIKU_DB_ERR_BUSY;
        }
        rc = finish_abort(db, t);
        if (rc != TIKU_DB_OK) {
            return rc;
        }
    }
    rc = tiku_db_eng_entry(db, t, TDB_OPEN, t->max_seq);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    t->open = 1;
    t->recover = 0;
    t->base = t->max_seq;
    return TIKU_DB_OK;
}

int tiku_db_commit(tiku_db_t *db, int table)
{
    tiku_db_tstate_t *t = table_of(db, table);
    int rc;

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (!t->open || t->recover) {
        return TIKU_DB_ERR_INVAL;
    }
    rc = tiku_db_eng_entry(db, t, TDB_CLOSED, t->base);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    t->open = 0;
    return TIKU_DB_OK;
}

int tiku_db_abort(tiku_db_t *db, int table)
{
    tiku_db_tstate_t *t = table_of(db, table);

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (!t->open) {
        return TIKU_DB_ERR_INVAL;
    }
    return finish_abort(db, t);
}

/*---------------------------------------------------------------------------*/
/* RECORDS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build record @p seq of @p vals in @p db->rec.
 *
 * @return TIKU_DB_OK, TIKU_DB_ERR_TYPE when a number is out of its column's
 *         range, TIKU_DB_ERR_INVAL when bytes are too long or missing
 */
static int build_record(tiku_db_t *db, const tiku_db_tstate_t *t,
                        uint32_t seq, const tiku_db_val_t *vals)
{
    uint8_t *rec = db->rec;
    uint8_t i;

    memset(rec, 0, t->rec_size);
    put32(rec, seq);
    put32(rec + tdb_echo_off(db, t), seq);
    if (db->media.tear == TIKU_DB_TEAR_LINE) {
        put32(rec + tdb_echo_off(db, t) + 4u, ~seq);
    }
    for (i = 0; i < t->ncols; ++i) {
        uint8_t *p = rec + t->col_off[i];
        const tiku_db_val_t *v = &vals[i];
        switch (tdb_col_type(db, t, i)) {
        case TIKU_DB_I32:
        case TIKU_DB_Q3:
            if (v->n < INT32_MIN || v->n > INT32_MAX) {
                return TIKU_DB_ERR_TYPE;
            }
            put32(p, (uint32_t)(int32_t)v->n);
            break;
        case TIKU_DB_U32:
            if (v->n < 0 || v->n > (int64_t)0xFFFFFFFF) {
                return TIKU_DB_ERR_TYPE;
            }
            put32(p, (uint32_t)v->n);
            break;
        case TIKU_DB_T64:
            put32(p, (uint32_t)(uint64_t)v->n);
            put32(p + 4, (uint32_t)((uint64_t)v->n >> 32));
            break;
        default:
            if (v->len > tdb_col_size(db, t, i) || (v->len != 0u && v->p == NULL)) {
                return TIKU_DB_ERR_INVAL;
            }
            memcpy(p, v->p, v->len);
            break;
        }
    }
    return TIKU_DB_OK;
}

int tiku_db_insert(tiku_db_t *db, int table, const tiku_db_val_t *vals,
                   uint32_t *seq)
{
    tiku_db_tstate_t *t = table_of(db, table);
    uint32_t next;
    int old_live;
    int rc;

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (vals == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    if (t->open && t->recover) {
        rc = finish_abort(db, t);
        if (rc != TIKU_DB_OK) {
            return rc;
        }
    }
    if (t->max_seq >= TDB_SEQ_NONE - 1u) {
        return TIKU_DB_ERR_FULL;
    }
    next = t->max_seq + 1u;
    old_live = tdb_slot_live(db, t, tdb_slot_off(t, next));
    if (old_live && t->policy == TIKU_DB_KEEP) {
        return TIKU_DB_ERR_FULL;
    }
    rc = build_record(db, t, next, vals);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    rc = tiku_db_eng_put(db, t, next);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    t->max_seq = next;
    if (!old_live) {
        t->count++;
    }
    if (seq != NULL) {
        *seq = next;
    }
    return TIKU_DB_OK;
}

int tiku_db_delete(tiku_db_t *db, int table, uint32_t seq)
{
    tiku_db_tstate_t *t = table_of(db, table);
    int rc;

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (seq < tdb_window_lo(t) || seq > t->max_seq || !tdb_live(db, t, seq)) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    rc = tiku_db_eng_kill(db, t, seq);
    if (rc != TIKU_DB_OK) {
        return rc;
    }
    t->count--;
    return TIKU_DB_OK;
}

/** @brief Fill @p row with record @p seq of @p table. */
static void fill_row(tiku_db_t *db, int table, uint32_t seq, tiku_db_row_t *row)
{
    row->rec = db->be->base + tdb_slot_off(&db->t[table], seq);
    row->seq = seq;
    row->table = (uint8_t)table;
    row->db = db;
}

int tiku_db_get(tiku_db_t *db, int table, uint32_t seq, tiku_db_row_t *row)
{
    tiku_db_tstate_t *t = table_of(db, table);

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (row == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    if (seq < tdb_window_lo(t) || seq > t->max_seq || !tdb_live(db, t, seq) ||
        !visible(t, seq)) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    fill_row(db, table, seq, row);
    return TIKU_DB_OK;
}

int64_t tiku_db_row_num(const tiku_db_row_t *row, uint8_t col)
{
    const tiku_db_tstate_t *t;
    if (row == NULL || row->db == NULL || row->table >= TIKU_DB_TABLES_MAX) {
        return 0;
    }
    t = &row->db->t[row->table];
    if (col >= t->ncols) {
        return 0;
    }
    return col_num(row->db, t, row->rec, col);
}

const void *tiku_db_row_bytes(const tiku_db_row_t *row, uint8_t col,
                              uint16_t *len)
{
    const tiku_db_tstate_t *t;
    const uint8_t *p;
    uint16_t size;

    if (len != NULL) {
        *len = 0;
    }
    if (row == NULL || row->db == NULL || row->table >= TIKU_DB_TABLES_MAX) {
        return NULL;
    }
    t = &row->db->t[row->table];
    if (col >= t->ncols) {
        return NULL;
    }
    p = row->rec + t->col_off[col];
    size = tdb_col_size(row->db, t, col);
    if (tdb_col_type(row->db, t, col) == TIKU_DB_TEXT) {
        const void *nul = memchr(p, 0, size);
        if (nul != NULL) {
            size = (uint16_t)((const uint8_t *)nul - p);
        }
    }
    if (len != NULL) {
        *len = size;
    }
    return p;
}

/*---------------------------------------------------------------------------*/
/* CURSORS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Set @p cur over @p table in the given direction with no filter. */
static int cursor_init(tiku_db_t *db, int table, int descending,
                       tiku_db_cursor_t *cur)
{
    tiku_db_tstate_t *t = table_of(db, table);

    if (t == NULL) {
        return TIKU_DB_ERR_NOTFOUND;
    }
    if (cur == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    memset(cur, 0, sizeof *cur);
    cur->db = db;
    cur->table = (uint8_t)table;
    cur->descending = (uint8_t)descending;
    cur->lo = tdb_window_lo(t);
    cur->hi = t->max_seq;
    cur->next = descending ? cur->hi : cur->lo;
    cur->left = 0xFFFFFFFFu;
    return TIKU_DB_OK;
}

int tiku_db_scan(tiku_db_t *db, int table, tiku_db_cursor_t *cur)
{
    return cursor_init(db, table, 0, cur);
}

int tiku_db_last(tiku_db_t *db, int table, uint32_t n, tiku_db_cursor_t *cur)
{
    int rc = cursor_init(db, table, 1, cur);
    if (rc == TIKU_DB_OK) {
        cur->left = n;
    }
    return rc;
}

int tiku_db_filter(tiku_db_cursor_t *cur, uint8_t col, uint8_t op,
                   const tiku_db_val_t *val)
{
    const tiku_db_tstate_t *t;
    tiku_db_filter_t *f;

    if (cur == NULL || cur->db == NULL || val == NULL ||
        cur->nfilters >= TIKU_DB_FILTERS_MAX || op < TIKU_DB_EQ || op > TIKU_DB_GE) {
        return TIKU_DB_ERR_INVAL;
    }
    t = &cur->db->t[cur->table];
    if (col >= t->ncols) {
        return TIKU_DB_ERR_INVAL;
    }
    if (!col_numeric(cur->db, t, col)) {
        if ((op != TIKU_DB_EQ && op != TIKU_DB_NE) ||
            val->len > tdb_col_size(cur->db, t, col) ||
            (val->len != 0u && val->p == NULL)) {
            return TIKU_DB_ERR_TYPE;
        }
    }
    f = &cur->filter[cur->nfilters++];
    f->col = col;
    f->op = op;
    f->val = *val;
    return TIKU_DB_OK;
}

int tiku_db_limit(tiku_db_cursor_t *cur, uint32_t n)
{
    if (cur == NULL || cur->db == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    cur->left = n;
    return TIKU_DB_OK;
}

int tiku_db_next(tiku_db_cursor_t *cur, tiku_db_row_t *row)
{
    tiku_db_t *db;
    const tiku_db_tstate_t *t;

    if (cur == NULL || cur->db == NULL || row == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    db = cur->db;
    if (!db->mounted || !db->t[cur->table].live) {
        return TIKU_DB_ERR_INVAL;
    }
    t = &db->t[cur->table];
    while (cur->left != 0u && cur->next >= cur->lo && cur->next <= cur->hi &&
           cur->next != 0u) {
        uint32_t seq = cur->next;
        uint8_t i, pass = 1;
        const uint8_t *rec;

        cur->next = cur->descending ? seq - 1u : seq + 1u;
        if (!tdb_live(db, t, seq) || !visible(t, seq)) {
            continue;
        }
        rec = db->be->base + tdb_slot_off(t, seq);
        for (i = 0; i < cur->nfilters && pass; ++i) {
            pass = filter_pass(db, t, rec, &cur->filter[i]);
        }
        if (!pass) {
            continue;
        }
        cur->left--;
        fill_row(db, cur->table, seq, row);
        return 1;
    }
    return 0;
}

int tiku_db_agg(tiku_db_cursor_t *cur, uint8_t col, tiku_db_agg_t *out)
{
    tiku_db_row_t row;
    int rc;

    if (cur == NULL || cur->db == NULL || out == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    if (col >= cur->db->t[cur->table].ncols ||
        !col_numeric(cur->db, &cur->db->t[cur->table], col)) {
        return TIKU_DB_ERR_TYPE;
    }
    memset(out, 0, sizeof *out);
    while ((rc = tiku_db_next(cur, &row)) == 1) {
        int64_t v = tiku_db_row_num(&row, col);
        if (out->count == 0u || v < out->min) {
            out->min = v;
        }
        if (out->count == 0u || v > out->max) {
            out->max = v;
        }
        out->sum += v;
        out->count++;
    }
    if (rc < 0) {
        return rc;
    }
    out->mean = out->count ? out->sum / (int64_t)out->count : 0;
    return TIKU_DB_OK;
}

int tiku_db_delete_matching(tiku_db_cursor_t *cur, uint32_t *deleted)
{
    tiku_db_row_t row;
    uint32_t n = 0;
    int rc;

    if (deleted != NULL) {
        *deleted = 0;
    }
    if (cur == NULL || cur->db == NULL) {
        return TIKU_DB_ERR_INVAL;
    }
    while ((rc = tiku_db_next(cur, &row)) == 1) {
        rc = tiku_db_delete(cur->db, cur->table, row.seq);
        if (rc != TIKU_DB_OK) {
            return rc;
        }
        n++;
        if (deleted != NULL) {
            *deleted = n;
        }
    }
    return rc < 0 ? rc : TIKU_DB_OK;
}
