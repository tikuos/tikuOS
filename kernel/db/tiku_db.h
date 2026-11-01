/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db.h - tables of fixed-size records in place on byte-addressable NVM.
 *
 * Each table is a ring of records ordered by a sequence number; an insert
 * commits with the record's own gate word, a delete clears it, and a
 * transaction hides its records behind one entry in a per-table commit ring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DB_H_
#define TIKU_DB_H_

#include <stddef.h>
#include <stdint.h>

#include "kernel/fs/tiku_nvm_backend.h"
#include "kernel/db/tiku_db_media.h"

/*---------------------------------------------------------------------------*/
/* COMPILE-TIME LIMITS                                                       */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_DB_TABLES_MAX
#define TIKU_DB_TABLES_MAX    8      /**< table entries in an extent          */
#endif
#ifndef TIKU_DB_COLS_MAX
#define TIKU_DB_COLS_MAX      8      /**< columns in a table                  */
#endif
#ifndef TIKU_DB_RECORD_MAX
#define TIKU_DB_RECORD_MAX    256    /**< bytes of one record, padding included */
#endif
#ifndef TIKU_DB_COMMIT_LINES
#define TIKU_DB_COMMIT_LINES  64     /**< entries in a table's commit ring    */
#endif
#define TIKU_DB_NAME_MAX      16     /**< table name bytes, NUL included      */
#define TIKU_DB_COL_NAME_MAX  14     /**< column name bytes, NUL included     */
#define TIKU_DB_FILTERS_MAX   2      /**< filters one cursor carries          */

/*---------------------------------------------------------------------------*/
/* RESULT CODES                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_DB_OK             0
#define TIKU_DB_ERR_INVAL     -1   /**< argument or state refused            */
#define TIKU_DB_ERR_IO        -2   /**< the backend refused a write           */
#define TIKU_DB_ERR_NOSTORE   -3   /**< no extent header at the base         */
#define TIKU_DB_ERR_CORRUPT   -4   /**< a header or entry fails its check    */
#define TIKU_DB_ERR_NOSPACE   -5   /**< no table entry or no room in the extent */
#define TIKU_DB_ERR_EXISTS    -6   /**< a live table has that name            */
#define TIKU_DB_ERR_NOTFOUND  -7   /**< no such table or record               */
#define TIKU_DB_ERR_FULL      -8   /**< a keep table's next slot is live, or seq is spent */
#define TIKU_DB_ERR_BUSY      -9   /**< a transaction is already open         */
#define TIKU_DB_ERR_LIFE     -10   /**< the ring wears out inside the declared life */
#define TIKU_DB_ERR_MEDIA    -11   /**< the extent was written for another medium or build */
#define TIKU_DB_ERR_TYPE     -12   /**< a value does not fit its column's type */

/*---------------------------------------------------------------------------*/
/* SCHEMA                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Column types; the numeric ones are read and written as int64_t. */
typedef enum {
    TIKU_DB_I32   = 1,   /**< signed 32-bit                       */
    TIKU_DB_U32   = 2,   /**< unsigned 32-bit                     */
    TIKU_DB_Q3    = 3,   /**< BASIC's fixed point, 1000 per unit  */
    TIKU_DB_T64   = 4,   /**< signed 64-bit time                  */
    TIKU_DB_TEXT  = 5,   /**< NUL-padded bytes, `size` of them    */
    TIKU_DB_BYTES = 6    /**< raw bytes, `size` of them           */
} tiku_db_type_t;

/** @brief Retention policy of a table. */
typedef enum {
    TIKU_DB_RING = 0,    /**< an insert into a full ring drops the oldest record */
    TIKU_DB_KEEP = 1     /**< an insert whose slot holds a live record is refused */
} tiku_db_policy_t;

/** @brief One column as a caller declares it. */
typedef struct {
    const char *name;    /**< 1 to 13 characters of [A-Za-z0-9_]            */
    uint8_t     type;    /**< tiku_db_type_t                                 */
    uint8_t     size;    /**< bytes of a TEXT or BYTES column; ignored otherwise */
} tiku_db_coldef_t;

/** @brief One column as the table stores it. */
typedef struct {
    char     name[TIKU_DB_COL_NAME_MAX];
    uint8_t  type;
    uint8_t  size;       /**< bytes the column occupies                        */
    uint16_t offset;     /**< byte offset inside a record                      */
} tiku_db_col_t;

/** @brief What a table is created with. */
typedef struct {
    uint32_t capacity;   /**< records in the ring, at least 1                  */
    uint8_t  policy;     /**< tiku_db_policy_t                                 */
    uint32_t rate;       /**< inserts per hour the table is declared for; 0: undeclared */
    uint32_t life;       /**< days the table must last; 0: undeclared          */
} tiku_db_spec_t;

/** @brief A table as mounted. */
typedef struct {
    char     name[TIKU_DB_NAME_MAX];
    uint8_t  ncols;
    uint8_t  policy;
    uint16_t rec_size;
    uint32_t capacity;
    uint32_t rate;
    uint32_t life;
} tiku_db_info_t;

/**
 * @brief A column value on the way in.
 *
 * Numeric columns take `n`; TEXT and BYTES take `len` bytes at `p`, at most
 * the column's size, and the rest of the column is zero.
 */
typedef struct {
    int64_t     n;
    const void *p;
    uint16_t    len;
} tiku_db_val_t;

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief SRAM state of one table entry; derived from the extent at mount. */
typedef struct {
    uint8_t  live;        /**< the entry holds a table                        */
    uint8_t  ncols;
    uint8_t  policy;
    uint8_t  open;        /**< the latest commit entry is OPEN                */
    uint8_t  recover;     /**< that OPEN entry was found at mount, not written here */
    uint8_t  reserved;
    uint16_t rec_size;
    uint16_t col_off[TIKU_DB_COLS_MAX];
    uint32_t capacity;
    uint32_t block_off;   /**< the table's block: commit ring, schema, ring   */
    uint32_t block_len;
    uint32_t commit_off;
    uint32_t schema_off;
    uint32_t ring_off;
    uint32_t max_seq;     /**< highest sequence number handed out             */
    uint32_t count;       /**< live records, hidden ones included             */
    uint32_t base;        /**< records above it are hidden while `open`       */
    uint32_t commit_n;    /**< counter of the latest commit entry             */
    uint32_t rate;
    uint32_t life;
} tiku_db_tstate_t;

/** @brief One mounted extent. */
typedef struct {
    tiku_nvm_backend_t *be;
    tiku_db_media_t     media;
    uint32_t            identity;
    uint32_t            extent;    /**< bytes of the extent                    */
    uint16_t            unit;      /**< bytes of a gate or echo write: max(line, 4) */
    uint16_t            stride;    /**< bytes between commit entries: max(line, 16) */
    uint8_t             commits;   /**< commit entries per table               */
    uint8_t             mounted;
    tiku_db_tstate_t    t[TIKU_DB_TABLES_MAX];
    uint8_t             rec[TIKU_DB_RECORD_MAX];   /**< the record being built */
} tiku_db_t;

/** @brief A record in place; valid until its slot is written again. */
typedef struct {
    const uint8_t    *rec;
    uint32_t          seq;
    uint8_t           table;
    const tiku_db_t  *db;
} tiku_db_row_t;

/** @brief Comparison operators of a filter. */
enum {
    TIKU_DB_EQ = 1, TIKU_DB_NE, TIKU_DB_LT, TIKU_DB_LE, TIKU_DB_GT, TIKU_DB_GE
};

/** @brief One column comparison a cursor applies to every record. */
typedef struct {
    uint8_t       col;
    uint8_t       op;
    tiku_db_val_t val;
} tiku_db_filter_t;

/** @brief A walk over one table's visible records in sequence order. */
typedef struct {
    tiku_db_t       *db;
    uint8_t          table;
    uint8_t          descending;
    uint8_t          nfilters;
    uint8_t          reserved;
    uint32_t         next;      /**< sequence number examined next           */
    uint32_t         lo;        /**< oldest sequence number the ring can hold */
    uint32_t         hi;        /**< newest sequence number at creation       */
    uint32_t         left;      /**< rows still to return; UINT32_MAX: no limit */
    tiku_db_filter_t filter[TIKU_DB_FILTERS_MAX];
} tiku_db_cursor_t;

/** @brief Aggregate of one numeric column over a cursor. */
typedef struct {
    uint32_t count;
    int64_t  min;
    int64_t  max;
    int64_t  sum;
    int64_t  mean;      /**< sum / count; 0 when count is 0                   */
} tiku_db_agg_t;

/** @brief What a table has done and how long it has left. */
typedef struct {
    uint32_t capacity;
    uint32_t count;
    uint32_t max_seq;
    uint32_t laps;      /**< inserts into the busiest slot: ceil(max_seq / capacity) */
    uint16_t rec_size;
    uint16_t lines;     /**< lines programmed per insert                      */
    uint8_t  txn_open;
    uint8_t  recovering; /**< the open transaction was left by an earlier session */
    uint32_t life_days; /**< days left at the declared rate; UINT32_MAX when unbounded */
    uint32_t rate;
    uint32_t life;
} tiku_db_stat_t;

/*---------------------------------------------------------------------------*/
/* EXTENT                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write an empty extent over @p be and mount it.
 *
 * A store already there is retired first, by clearing its magic, so a cut
 * leaves that store whole or TIKU_DB_ERR_NOSTORE, never part of its tables.
 *
 * @param identity  Stored in the header and reported by tiku_db_identity()
 * @return TIKU_DB_OK, TIKU_DB_ERR_INVAL or TIKU_DB_ERR_IO
 */
int tiku_db_format(tiku_db_t *db, tiku_nvm_backend_t *be,
                   const tiku_db_media_t *media, uint32_t identity);

/**
 * @brief Mount the extent at @p be: read the header, derive every table.
 *
 * Mount reads and never writes.  The medium descriptor must match the one
 * the extent was formatted with.
 *
 * @return TIKU_DB_OK, TIKU_DB_ERR_NOSTORE, TIKU_DB_ERR_MEDIA or TIKU_DB_ERR_CORRUPT
 */
int tiku_db_mount(tiku_db_t *db, tiku_nvm_backend_t *be,
                  const tiku_db_media_t *media);

/** @brief The identity the extent was formatted with; 0 when not mounted. */
uint32_t tiku_db_identity(const tiku_db_t *db);

/*---------------------------------------------------------------------------*/
/* TABLES                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Create a table; its ring is zeroed and its entry's gate written last.
 *
 * With a rate, a life and a medium endurance, the ring must outlast the life
 * at that rate, or the call returns TIKU_DB_ERR_LIFE and the capacity that
 * would do in @p needed.
 *
 * @return The table index, or a negative result code
 */
int tiku_db_create(tiku_db_t *db, const char *name,
                   const tiku_db_coldef_t *cols, uint8_t ncols,
                   const tiku_db_spec_t *spec, uint32_t *needed);

/** @brief Retire a table by clearing its entry's gate; its block is reusable. */
int tiku_db_drop(tiku_db_t *db, int table);

/** @brief The index of the live table named @p name, or TIKU_DB_ERR_NOTFOUND. */
int tiku_db_open(const tiku_db_t *db, const char *name);

/** @brief Whether entry @p table holds a live table. */
int tiku_db_table_live(const tiku_db_t *db, int table);

/** @brief Describe a live table. */
int tiku_db_info(const tiku_db_t *db, int table, tiku_db_info_t *info);

/** @brief Describe column @p col of a live table. */
int tiku_db_column(const tiku_db_t *db, int table, uint8_t col,
                   tiku_db_col_t *out);

/** @brief Counters and the life left of a live table. */
int tiku_db_stat(const tiku_db_t *db, int table, tiku_db_stat_t *st);

/**
 * @brief The capacity a table needs to outlast @p spec on @p media.
 *
 * @return 0 when the rate, the life or the endurance is undeclared
 */
uint32_t tiku_db_capacity_for_life(const tiku_db_media_t *media,
                                   const tiku_db_spec_t *spec);

/*---------------------------------------------------------------------------*/
/* RECORDS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Insert one record of @p vals, one value per column, in schema order.
 *
 * The record takes the next sequence number; a ring table drops the record
 * in its slot, a keep table refuses with TIKU_DB_ERR_FULL while that slot is
 * live.  After TIKU_DB_ERR_IO the SRAM state may lag the medium: remount.
 *
 * @param seq  Receives the record's sequence number
 */
int tiku_db_insert(tiku_db_t *db, int table, const tiku_db_val_t *vals,
                   uint32_t *seq);

/** @brief Clear the gate of record @p seq; TIKU_DB_ERR_NOTFOUND if not live. */
int tiku_db_delete(tiku_db_t *db, int table, uint32_t seq);

/** @brief The visible record @p seq, or TIKU_DB_ERR_NOTFOUND. */
int tiku_db_get(tiku_db_t *db, int table, uint32_t seq, tiku_db_row_t *row);

/** @brief Numeric column @p col of @p row widened to 64 bits; 0 for TEXT and BYTES. */
int64_t tiku_db_row_num(const tiku_db_row_t *row, uint8_t col);

/**
 * @brief Column @p col of @p row in place.
 *
 * @param len  Receives the bytes: up to the first NUL for TEXT, the column's
 *             size otherwise
 */
const void *tiku_db_row_bytes(const tiku_db_row_t *row, uint8_t col,
                              uint16_t *len);

/*---------------------------------------------------------------------------*/
/* TRANSACTIONS                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Open a transaction: inserts until commit are hidden from every reader.
 *
 * One transaction per table; a second open returns TIKU_DB_ERR_BUSY.  An
 * open transaction left by an earlier session is aborted here first.
 */
int tiku_db_begin(tiku_db_t *db, int table);

/** @brief Make the open transaction's records visible with one entry write. */
int tiku_db_commit(tiku_db_t *db, int table);

/** @brief Delete the open transaction's records, then close the entry. */
int tiku_db_abort(tiku_db_t *db, int table);

/*---------------------------------------------------------------------------*/
/* QUERIES                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief A cursor over every visible record, oldest first. */
int tiku_db_scan(tiku_db_t *db, int table, tiku_db_cursor_t *cur);

/** @brief A cursor over the newest @p n visible records, newest first. */
int tiku_db_last(tiku_db_t *db, int table, uint32_t n, tiku_db_cursor_t *cur);

/**
 * @brief Keep only records where column @p col compares true against @p val.
 *
 * A TEXT or BYTES column takes TIKU_DB_EQ and TIKU_DB_NE; up to
 * TIKU_DB_FILTERS_MAX filters, all of which must hold.
 */
int tiku_db_filter(tiku_db_cursor_t *cur, uint8_t col, uint8_t op,
                   const tiku_db_val_t *val);

/** @brief Return at most @p n more rows. */
int tiku_db_limit(tiku_db_cursor_t *cur, uint32_t n);

/** @brief The next row: 1 with @p row filled, 0 at the end, negative on error. */
int tiku_db_next(tiku_db_cursor_t *cur, tiku_db_row_t *row);

/** @brief Count, min, max, sum and mean of numeric column @p col over @p cur. */
int tiku_db_agg(tiku_db_cursor_t *cur, uint8_t col, tiku_db_agg_t *out);

/**
 * @brief Delete every record @p cur returns, oldest first.
 *
 * Each delete commits on its own; a cut leaves a prefix of them done.
 *
 * @param deleted  Receives the count
 */
int tiku_db_delete_matching(tiku_db_cursor_t *cur, uint32_t *deleted);

#endif /* TIKU_DB_H_ */
