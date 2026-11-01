/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_store.h - the extent layout shared by tiku_db.c and its engine.
 *
 * Offsets of the header, the table entries, a table's block and a record;
 * the small reads every part of the engine does; the engine entry points
 * tiku_db.c calls.  Nothing outside kernel/db/ includes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DB_STORE_H_
#define TIKU_DB_STORE_H_

#include <string.h>

#include "kernel/db/tiku_db.h"

/*---------------------------------------------------------------------------*/
/* EXTENT LAYOUT                                                             */
/*---------------------------------------------------------------------------*/
/*
 * header       64 B   magic last; version, line, tear, limits, identity, size, check
 * tables       TIKU_DB_TABLES_MAX x 64 B   gate last; name, schema size, block, check
 * blocks       per table, 64-aligned: commit ring | schema | ring
 *
 * A record is [gate u32][columns][echo u32], padded to the line.  On a
 * medium whose line cannot tear the gate and echo share lines with columns;
 * elsewhere the gate has the first unit to itself and the echo the last,
 * and where a line can tear the echo is followed by its complement, so a
 * line left as garbage never reads as a sequence number.  A record is live
 * when gate == echo, neither 0 nor ~0, and the value's slot is the record's.
 * A commit entry is [n][base][kind][check].
 */

#define TDB_MAGIC          0x31424454u   /* "TDB1" as little-endian bytes */
#define TDB_VERSION        1u
#define TDB_HDR_BYTES      64u
#define TDB_TENT_BYTES     64u
#define TDB_TABLE_LIVE     0x4C424154u   /* "TABL" */
#define TDB_CENT_BYTES     16u
#define TDB_COL_BYTES      16u
#define TDB_SCHEMA_BYTES   ((uint32_t)TIKU_DB_COLS_MAX * TDB_COL_BYTES)
#define TDB_BLOCK_ALIGN    64u
#define TDB_SEQ_NONE       0xFFFFFFFFu
#define TDB_OPEN           1u
#define TDB_CLOSED         2u

#define TDB_DYN_OFF  TDB_ALIGN(TDB_HDR_BYTES + TIKU_DB_TABLES_MAX * TDB_TENT_BYTES, \
                               TDB_BLOCK_ALIGN)

/* header */
#define TDB_H_MAGIC     0u
#define TDB_H_VERSION   4u    /* u16 */
#define TDB_H_LINE      6u    /* u16 */
#define TDB_H_TEAR      8u    /* u8  */
#define TDB_H_TABLES    9u    /* u8  */
#define TDB_H_COMMITS   10u   /* u8  */
#define TDB_H_IDENTITY  12u
#define TDB_H_EXTENT    16u
#define TDB_H_CHECK     60u   /* FNV-1a over bytes 4..59 */

/* table entry */
#define TDB_T_GATE      0u
#define TDB_T_NAME      4u    /* TIKU_DB_NAME_MAX bytes */
#define TDB_T_NCOLS     20u
#define TDB_T_POLICY    21u
#define TDB_T_RECSIZE   24u   /* u16 */
#define TDB_T_CAP       28u
#define TDB_T_BLOCK     32u
#define TDB_T_BLOCKLEN  36u
#define TDB_T_RATE      40u
#define TDB_T_LIFE      44u
#define TDB_T_CHECK     60u   /* FNV-1a over bytes 4..59 */

/* schema column */
#define TDB_C_TYPE      0u
#define TDB_C_SIZE      1u
#define TDB_C_NAME      2u    /* TIKU_DB_COL_NAME_MAX bytes */

/* commit entry */
#define TDB_E_N         0u
#define TDB_E_BASE      4u
#define TDB_E_KIND      8u
#define TDB_E_CHECK     12u   /* FNV-1a over bytes 0..11 */

/** @brief Round @p n up to a multiple of @p a, a power of two. */
#define TDB_ALIGN(n, a)  ((((uint32_t)(n)) + ((uint32_t)(a) - 1u)) & ~((uint32_t)(a) - 1u))

/*---------------------------------------------------------------------------*/
/* READS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Alignment-safe 32-bit read at byte offset @p off of the extent. */
static inline uint32_t tdb_rd32(const tiku_db_t *db, uint32_t off)
{
    uint32_t v;
    memcpy(&v, db->be->base + off, sizeof v);
    return v;
}

/** @brief Alignment-safe 16-bit read at byte offset @p off of the extent. */
static inline uint16_t tdb_rd16(const tiku_db_t *db, uint32_t off)
{
    uint16_t v;
    memcpy(&v, db->be->base + off, sizeof v);
    return v;
}

/** @brief Byte offset of the slot that holds sequence number @p seq. */
static inline uint32_t tdb_slot_off(const tiku_db_tstate_t *t, uint32_t seq)
{
    return t->ring_off + (seq % t->capacity) * t->rec_size;
}

/** @brief Whether @p v can be a sequence number stored in a gate or echo. */
static inline int tdb_seq_valid(uint32_t v)
{
    return v != 0u && v != TDB_SEQ_NONE;
}

/** @brief Byte offset of the echo inside a record of @p t. */
static inline uint32_t tdb_echo_off(const tiku_db_t *db,
                                    const tiku_db_tstate_t *t)
{
    return db->media.tear == TIKU_DB_TEAR_LINE ?
           (uint32_t)t->rec_size - db->unit : (uint32_t)t->rec_size - 4u;
}

/**
 * @brief The echo of the record at byte offset @p off; 0 when it is not one.
 *
 * Where a line can tear the echo counts only with its complement beside it.
 */
static inline uint32_t tdb_echo(const tiku_db_t *db, const tiku_db_tstate_t *t,
                                uint32_t off)
{
    uint32_t at = off + tdb_echo_off(db, t);
    uint32_t e = tdb_rd32(db, at);
    if (db->media.tear == TIKU_DB_TEAR_LINE && tdb_rd32(db, at + 4u) != ~e) {
        return 0u;
    }
    return tdb_seq_valid(e) ? e : 0u;
}

/**
 * @brief Whether record @p seq is live: its slot's gate and echo both say so.
 */
static inline int tdb_live(const tiku_db_t *db, const tiku_db_tstate_t *t,
                           uint32_t seq)
{
    uint32_t off = tdb_slot_off(t, seq);
    return tdb_seq_valid(seq) && tdb_rd32(db, off) == seq &&
           tdb_echo(db, t, off) == seq;
}

/** @brief Whether the slot at byte offset @p off holds a live record. */
static inline int tdb_slot_live(const tiku_db_t *db, const tiku_db_tstate_t *t,
                                uint32_t off)
{
    uint32_t gate = tdb_rd32(db, off);
    return tdb_seq_valid(gate) && tdb_echo(db, t, off) == gate;
}

/** @brief Oldest sequence number the ring of @p t can still hold. */
static inline uint32_t tdb_window_lo(const tiku_db_tstate_t *t)
{
    return t->max_seq >= t->capacity ? t->max_seq - t->capacity + 1u : 1u;
}

/** @brief The schema's type byte of column @p col. */
static inline uint8_t tdb_col_type(const tiku_db_t *db,
                                   const tiku_db_tstate_t *t, uint8_t col)
{
    return db->be->base[t->schema_off + col * TDB_COL_BYTES + TDB_C_TYPE];
}

/** @brief The schema's size byte of column @p col. */
static inline uint8_t tdb_col_size(const tiku_db_t *db,
                                   const tiku_db_tstate_t *t, uint8_t col)
{
    return db->be->base[t->schema_off + col * TDB_COL_BYTES + TDB_C_SIZE];
}

/*---------------------------------------------------------------------------*/
/* ENGINE                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Bytes a column of @p type and declared @p size occupies; 0 if invalid. */
uint8_t tiku_db_eng_type_bytes(uint8_t type, uint8_t size);

/**
 * @brief Record size and column offsets for @p schema on @p media.
 *
 * @param schema  ncols columns of TDB_COL_BYTES each, as stored
 * @return TIKU_DB_OK, TIKU_DB_ERR_TYPE or TIKU_DB_ERR_INVAL (too long)
 */
int tiku_db_eng_layout(const tiku_db_media_t *media, const uint8_t *schema,
                       uint8_t ncols, uint16_t *rec_size, uint16_t *col_off);

/** @brief Bytes a table of @p capacity records of @p rec_size needs. */
uint32_t tiku_db_eng_block_len(const tiku_db_t *db, uint32_t capacity,
                               uint16_t rec_size);

/**
 * @brief Write table @p idx: zero its block, the schema, the entry, the gate.
 *
 * The entry's body is complete before its gate; a cut before the gate
 * leaves no table.  On return the entry's SRAM state is derived as at mount.
 */
int tiku_db_eng_create(tiku_db_t *db, uint8_t idx, const char *name,
                       const uint8_t *schema, uint8_t ncols,
                       const tiku_db_spec_t *spec, uint16_t rec_size,
                       uint32_t block_off, uint32_t block_len);

/** @brief Clear table @p idx's gate. */
int tiku_db_eng_drop(tiku_db_t *db, uint8_t idx);

/**
 * @brief Write record @p seq from @p db->rec: echo unit, middle, gate unit.
 *
 * The slot's previous record is unreadable once the echo unit has landed and
 * the new one readable once the gate unit has; a cut in between leaves the
 * slot empty.
 */
int tiku_db_eng_put(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t seq);

/** @brief Clear the gate of record @p seq. */
int tiku_db_eng_kill(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t seq);

/** @brief Append a commit entry of @p kind and @p base to @p t's ring. */
int tiku_db_eng_entry(tiku_db_t *db, tiku_db_tstate_t *t, uint32_t kind,
                      uint32_t base);

#endif /* TIKU_DB_STORE_H_ */
