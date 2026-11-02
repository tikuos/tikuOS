/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_db.c - "db" command implementation.
 *
 * Parses each verb's arguments against the table's schema, calls tiku_db,
 * and prints rows, aggregates and refusals as text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include <shell/tiku_shell.h>

#if TIKU_SHELL_CMD_DB

#include "tiku_shell_cmd_db.h"
#include <kernel/db/tiku_db_board.h>
#include <kernel/cpu/tiku_bench.h>
#include <kernel/timers/tiku_clock.h>
#include <stdint.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* NUMBERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Render @p v in decimal into @p out (at least 21 bytes). */
static const char *i64_text(int64_t v, char *out)
{
    char tmp[21];
    uint64_t u = (v < 0) ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    int n = 0, i = 0;

    do {
        tmp[n++] = (char)('0' + (int)(u % 10u));
        u /= 10u;
    } while (u != 0u);
    if (v < 0) {
        out[i++] = '-';
    }
    while (n > 0) {
        out[i++] = tmp[--n];
    }
    out[i] = '\0';
    return out;
}

/** @brief Render a Q.3 value as "<int>.<3 digits>" into @p out (24 bytes). */
static const char *q3_text(int64_t v, char *out)
{
    char whole[21];
    uint64_t u = (v < 0) ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    size_t n;

    out[0] = '\0';
    if (v < 0) {
        strcpy(out, "-");
    }
    strcat(out, i64_text((int64_t)(u / 1000u), whole));
    n = strlen(out);
    out[n] = '.';
    out[n + 1] = (char)('0' + (int)((u / 100u) % 10u));
    out[n + 2] = (char)('0' + (int)((u / 10u) % 10u));
    out[n + 3] = (char)('0' + (int)(u % 10u));
    out[n + 4] = '\0';
    return out;
}

/**
 * @brief Parse a signed decimal or 0x-prefixed hexadecimal integer.
 *
 * @return 0 on success, -1 on a malformed or out-of-range number
 */
static int parse_i64(const char *s, int64_t *out)
{
    uint64_t v = 0;
    int neg = 0, digits = 0;
    unsigned base = 10u;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        s += 2;
    }
    for (; *s != '\0'; ++s) {
        unsigned d;
        if (*s >= '0' && *s <= '9') {
            d = (unsigned)(*s - '0');
        } else if (base == 16u && *s >= 'a' && *s <= 'f') {
            d = (unsigned)(*s - 'a' + 10);
        } else if (base == 16u && *s >= 'A' && *s <= 'F') {
            d = (unsigned)(*s - 'A' + 10);
        } else {
            return -1;
        }
        if (v > (UINT64_MAX - d) / base) {
            return -1;
        }
        v = v * base + d;
        digits++;
    }
    if (digits == 0 || v > (uint64_t)INT64_MAX + (uint64_t)neg) {
        return -1;
    }
    *out = neg ? (int64_t)(0 - v) : (int64_t)v;
    return 0;
}

/**
 * @brief Parse a Q.3 number: decimal digits, then up to three fraction digits.
 *
 * "21.5" is 21500, "-0.25" is -250, ".5" is 500; "1." and "1.2345" are refused.
 */
static int parse_q3(const char *s, int64_t *out)
{
    uint64_t whole = 0, frac = 0;
    int neg = 0, wdigits = 0, fdigits = 0;

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    for (; *s >= '0' && *s <= '9'; ++s) {
        if (whole > ((uint64_t)INT64_MAX / 1000u - 9u) / 10u) {
            return -1;
        }
        whole = whole * 10u + (uint64_t)(*s - '0');
        wdigits++;
    }
    if (*s == '.') {
        for (++s; *s >= '0' && *s <= '9'; ++s) {
            if (fdigits == 3) {
                return -1;
            }
            frac = frac * 10u + (uint64_t)(*s - '0');
            fdigits++;
        }
        if (fdigits == 0) {
            return -1;
        }
    }
    if (*s != '\0' || wdigits + fdigits == 0) {
        return -1;
    }
    for (; fdigits < 3; ++fdigits) {
        frac *= 10u;
    }
    *out = (int64_t)(whole * 1000u + frac);
    if (neg) {
        *out = -*out;
    }
    return 0;
}

/**
 * @brief Microseconds since boot from the kernel clock.
 *
 * The tick counter wraps (16 bits on MSP430), so the value restarts after
 * the counter's period.
 */
static int64_t now_us(void)
{
    return (int64_t)tiku_clock_time() * 1000000 / (int64_t)TIKU_CLOCK_SECOND;
}

/*---------------------------------------------------------------------------*/
/* TABLE HELPERS                                                             */
/*---------------------------------------------------------------------------*/

/* Bytes values parsed from hexadecimal, for the one verb that is running: an
 * insert's columns take consecutive slices, a range's two filters a half
 * each.  A record holds at most TIKU_DB_RECORD_MAX bytes of columns. */
static uint8_t scratch[TIKU_DB_RECORD_MAX];

/** @brief The board's database, or NULL after printing why there is none. */
static tiku_db_t *board(void)
{
    tiku_db_t *db = tiku_db_board();
    if (db == NULL) {
        SHELL_PRINTF("db: %s\n", tiku_db_board_why());
    }
    return db;
}

/** @brief The table named @p name, or -1 after printing why. */
static int table(tiku_db_t *db, const char *name)
{
    int t = tiku_db_open(db, name);
    if (t < 0) {
        SHELL_PRINTF("db: no table %s\n", name);
    }
    return t;
}

/** @brief The index of column @p name in table @p t, or -1 after printing. */
static int column(tiku_db_t *db, int t, const char *name)
{
    tiku_db_col_t c;
    uint8_t i;

    for (i = 0; tiku_db_column(db, t, i, &c) == TIKU_DB_OK; ++i) {
        if (strcmp(c.name, name) == 0) {
            return i;
        }
    }
    SHELL_PRINTF("db: no column %s\n", name);
    return -1;
}

/** @brief A type's name. */
static const char *type_name(uint8_t type)
{
    switch (type) {
    case TIKU_DB_I32:   return "i32";
    case TIKU_DB_U32:   return "u32";
    case TIKU_DB_Q3:    return "q3";
    case TIKU_DB_T64:   return "t64";
    case TIKU_DB_TEXT:  return "text";
    case TIKU_DB_BYTES: return "bytes";
    default:            return "?";
    }
}

/** @brief Value of one hexadecimal digit, or -1. */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Parse @p s as a value of column @p c into @p v.
 *
 * Text is taken as written; bytes are hexadecimal pairs into @p buf; t64
 * also takes "now".
 *
 * @return 0, or -1 after printing why
 */
static int parse_value(const tiku_db_col_t *c, const char *s, tiku_db_val_t *v,
                       uint8_t *buf, size_t buf_len)
{
    int rc = 0;

    memset(v, 0, sizeof *v);
    switch (c->type) {
    case TIKU_DB_Q3:
        rc = parse_q3(s, &v->n);
        break;
    case TIKU_DB_T64:
        if (strcmp(s, "now") == 0) {
            v->n = now_us();
        } else {
            rc = parse_i64(s, &v->n);
        }
        break;
    case TIKU_DB_TEXT:
        if (strlen(s) > c->size) {
            SHELL_PRINTF("db: %s is %u bytes; %s holds %u\n", s,
                         (unsigned)strlen(s), c->name, (unsigned)c->size);
            return -1;
        }
        v->p = s;
        v->len = (uint16_t)strlen(s);
        break;
    case TIKU_DB_BYTES: {
        size_t n = strlen(s), i;
        if (n % 2u != 0u || n / 2u > buf_len) {
            rc = -1;
            break;
        }
        for (i = 0; i < n / 2u; ++i) {
            int hi = hex_digit(s[2u * i]), lo = hex_digit(s[2u * i + 1u]);
            if (hi < 0 || lo < 0) {
                rc = -1;
                break;
            }
            buf[i] = (uint8_t)(hi * 16 + lo);
        }
        v->p = buf;
        v->len = (uint16_t)(n / 2u);
        break;
    }
    default:
        rc = parse_i64(s, &v->n);
        break;
    }
    if (rc != 0) {
        SHELL_PRINTF("db: %s is not a %s for %s\n", s, type_name(c->type), c->name);
    }
    return rc;
}

/** @brief Print column @p col of @p row. */
static void print_value(const tiku_db_row_t *row, const tiku_db_col_t *c,
                        uint8_t col)
{
    char num[24];
    uint16_t len, i;
    const uint8_t *p;

    switch (c->type) {
    case TIKU_DB_TEXT:
        p = (const uint8_t *)tiku_db_row_bytes(row, col, &len);
        for (i = 0; i < len; ++i) {
            SHELL_PRINTF("%c", (p[i] >= 0x20u && p[i] < 0x7Fu) ? p[i] : '.');
        }
        break;
    case TIKU_DB_BYTES:
        p = (const uint8_t *)tiku_db_row_bytes(row, col, &len);
        for (i = 0; i < len; ++i) {
            SHELL_PRINTF("%02x", (unsigned)p[i]);
        }
        break;
    case TIKU_DB_Q3:
        SHELL_PRINTF("%s", q3_text(tiku_db_row_num(row, col), num));
        break;
    default:
        SHELL_PRINTF("%s", i64_text(tiku_db_row_num(row, col), num));
        break;
    }
}

/** @brief Print one row: its sequence number, then each column. */
static void print_row(tiku_db_t *db, int t, const tiku_db_row_t *row)
{
    tiku_db_col_t c;
    uint8_t i;

    SHELL_PRINTF("%lu", (unsigned long)row->seq);
    for (i = 0; tiku_db_column(db, t, i, &c) == TIKU_DB_OK; ++i) {
        SHELL_PRINTF("\t");
        print_value(row, &c, i);
    }
    SHELL_PRINTF("\n");
}

/** @brief Print every row @p cur returns; the count last. */
static void print_rows(tiku_db_t *db, int t, tiku_db_cursor_t *cur)
{
    tiku_db_row_t row;
    unsigned long n = 0;
    int rc;

    while ((rc = tiku_db_next(cur, &row)) == 1) {
        print_row(db, t, &row);
        n++;
    }
    if (rc < 0) {
        SHELL_PRINTF("db: %s\n", tiku_db_err_name(rc));
        return;
    }
    SHELL_PRINTF("(%lu rows)\n", n);
}

/** @brief A comparison operator's code, or 0. */
static uint8_t op_code(const char *s)
{
    if (strcmp(s, "=") == 0 || strcmp(s, "==") == 0) return TIKU_DB_EQ;
    if (strcmp(s, "!=") == 0) return TIKU_DB_NE;
    if (strcmp(s, "<") == 0) return TIKU_DB_LT;
    if (strcmp(s, "<=") == 0) return TIKU_DB_LE;
    if (strcmp(s, ">") == 0) return TIKU_DB_GT;
    if (strcmp(s, ">=") == 0) return TIKU_DB_GE;
    return 0;
}

/**
 * @brief Add the filter "<col> <op> <value>" from @p argv to @p cur.
 *
 * @p keep holds a bytes value for as long as the cursor runs.
 * @return 0, or -1 after printing why
 */
static int add_filter(tiku_db_t *db, int t, tiku_db_cursor_t *cur,
                      const char *const *argv, uint8_t *keep, size_t keep_len)
{
    tiku_db_col_t c;
    tiku_db_val_t v;
    int col = column(db, t, argv[0]);
    uint8_t op = op_code(argv[1]);
    int rc;

    if (col < 0) {
        return -1;
    }
    if (op == 0u) {
        SHELL_PRINTF("db: %s is not one of = != < <= > >=\n", argv[1]);
        return -1;
    }
    (void)tiku_db_column(db, t, (uint8_t)col, &c);
    if (parse_value(&c, argv[2], &v, keep, keep_len) != 0) {
        return -1;
    }
    rc = tiku_db_filter(cur, (uint8_t)col, op, &v);
    if (rc != TIKU_DB_OK) {
        SHELL_PRINTF("db: filter %s %s: %s\n", argv[0], argv[1],
                     tiku_db_err_name(rc));
        return -1;
    }
    return 0;
}

/** @brief Parse a count that must be positive. */
static int parse_count(const char *s, uint32_t *out)
{
    int64_t v;
    if (parse_i64(s, &v) != 0 || v <= 0 || v > 0x7FFFFFFF) {
        SHELL_PRINTF("db: %s is not a count\n", s);
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* VERBS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief db status: the extent and how it is used. */
static void verb_status(void)
{
    const tiku_db_media_t *m = tiku_db_board_media();
    uint32_t off = 0, len = 0;
    tiku_db_t *db;
    int i, tables = 0;

    SHELL_PRINTF("medium: line %u B; %s\n", (unsigned)m->unit,
                 m->tear == TIKU_DB_TEAR_NONE ? "a cut leaves a line whole or old" :
                 m->tear == TIKU_DB_TEAR_BYTE ? "a cut lands a prefix of a write" :
                 "a cut can leave a line unreadable");
    if (m->endurance != 0u) {
        SHELL_PRINTF("endurance: %lu programs per line\n", (unsigned long)m->endurance);
    } else {
        SHELL_PRINTF("endurance: no limit\n");
    }
    if (!tiku_db_board_extent(&off, &len)) {
        SHELL_PRINTF("extent: none; set one with layout stage nvm.db=<bytes>\n");
        return;
    }
    SHELL_PRINTF("extent: %lu bytes at region offset %lu\n",
                 (unsigned long)len, (unsigned long)off);
    db = tiku_db_board();
    if (db == NULL) {
        SHELL_PRINTF("store: %s\n", tiku_db_board_why());
        return;
    }
    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        tables += tiku_db_table_live(db, i);
    }
    SHELL_PRINTF("store: mounted, %d of %d tables\n", tables, TIKU_DB_TABLES_MAX);
}

/** @brief db tables: one line per table. */
static void verb_tables(tiku_db_t *db)
{
    tiku_db_info_t info;
    tiku_db_stat_t st;
    int i, n = 0;

    for (i = 0; i < TIKU_DB_TABLES_MAX; ++i) {
        if (tiku_db_info(db, i, &info) != TIKU_DB_OK ||
            tiku_db_stat(db, i, &st) != TIKU_DB_OK) {
            continue;
        }
        SHELL_PRINTF("%s\t%lu/%lu rows\t%u B/row\t%s\n", info.name,
                     (unsigned long)st.count, (unsigned long)st.capacity,
                     (unsigned)info.rec_size,
                     info.policy == TIKU_DB_KEEP ? "keep" : "ring");
        n++;
    }
    SHELL_PRINTF("(%d tables)\n", n);
}

/** @brief db schema <t>: the columns and the declared rate and life. */
static void verb_schema(tiku_db_t *db, int t)
{
    tiku_db_info_t info;
    tiku_db_col_t c;
    uint8_t i;

    (void)tiku_db_info(db, t, &info);
    for (i = 0; tiku_db_column(db, t, i, &c) == TIKU_DB_OK; ++i) {
        if (c.type == TIKU_DB_TEXT || c.type == TIKU_DB_BYTES) {
            SHELL_PRINTF("%s:%s:%u\n", c.name, type_name(c.type), (unsigned)c.size);
        } else {
            SHELL_PRINTF("%s:%s\n", c.name, type_name(c.type));
        }
    }
    SHELL_PRINTF("cap=%lu %s rate=%lu/h life=%lud\n", (unsigned long)info.capacity,
                 info.policy == TIKU_DB_KEEP ? "keep" : "ring",
                 (unsigned long)info.rate, (unsigned long)info.life);
}

/** @brief db stat <t>: counters and the life left. */
static void verb_stat(tiku_db_t *db, int t)
{
    tiku_db_stat_t st;

    (void)tiku_db_stat(db, t, &st);
    SHELL_PRINTF("rows %lu of %lu, last seq %lu, laps %lu\n",
                 (unsigned long)st.count, (unsigned long)st.capacity,
                 (unsigned long)st.max_seq, (unsigned long)st.laps);
    SHELL_PRINTF("record %u B, %u lines per insert\n", (unsigned)st.rec_size,
                 (unsigned)st.lines);
    if (st.life_days == 0xFFFFFFFFu) {
        SHELL_PRINTF("life: unbounded (no rate declared, or no endurance limit)\n");
    } else {
        SHELL_PRINTF("life: %lu days left at %lu inserts an hour\n",
                     (unsigned long)st.life_days, (unsigned long)st.rate);
    }
    if (st.txn_open) {
        SHELL_PRINTF("transaction: open%s\n",
                     st.recovering ? ", left by an earlier session" : "");
    }
}

/**
 * @brief Parse a rate: N/s, N/m, N/h or N/d, as inserts an hour.
 *
 * @return 0, or -1 on a malformed rate
 */
static int parse_rate(const char *s, uint32_t *out)
{
    char num[12];
    const char *slash = strchr(s, '/');
    int64_t n;
    uint64_t per_hour;

    if (slash == NULL || (size_t)(slash - s) >= sizeof num || slash[2] != '\0') {
        return -1;
    }
    memcpy(num, s, (size_t)(slash - s));
    num[slash - s] = '\0';
    if (parse_i64(num, &n) != 0 || n <= 0) {
        return -1;
    }
    switch (slash[1]) {
    case 's': per_hour = (uint64_t)n * 3600u; break;
    case 'm': per_hour = (uint64_t)n * 60u;   break;
    case 'h': per_hour = (uint64_t)n;         break;
    case 'd': per_hour = ((uint64_t)n + 23u) / 24u; break;
    default:  return -1;
    }
    if (per_hour > 0xFFFFFFFFu) {
        return -1;
    }
    *out = (uint32_t)per_hour;
    return 0;
}

/** @brief Parse a life: Nd or Ny, as days. */
static int parse_life(const char *s, uint32_t *out)
{
    char num[12];
    size_t n = strlen(s);
    int64_t v;

    if (n < 2u || n - 1u >= sizeof num) {
        return -1;
    }
    memcpy(num, s, n - 1u);
    num[n - 1u] = '\0';
    if (parse_i64(num, &v) != 0 || v <= 0 || v > 1000000) {
        return -1;
    }
    if (s[n - 1u] == 'd') {
        *out = (uint32_t)v;
    } else if (s[n - 1u] == 'y') {
        *out = (uint32_t)(v * 365);
    } else {
        return -1;
    }
    return 0;
}

/**
 * @brief db create <t> <col:type[:size]>... [cap=N] [ring|keep]
 *        [rate=N/s|m|h|d] [life=Nd|Ny]
 */
static void verb_create(tiku_db_t *db, uint8_t argc, const char *argv[])
{
    static char names[TIKU_DB_COLS_MAX][TIKU_DB_COL_NAME_MAX];
    tiku_db_coldef_t cols[TIKU_DB_COLS_MAX];
    tiku_db_spec_t spec;
    uint8_t ncols = 0, i;
    uint32_t needed = 0;
    int rc;

    memset(&spec, 0, sizeof spec);
    spec.capacity = 64;
    spec.policy = TIKU_DB_RING;
    for (i = 3; i < argc; ++i) {
        const char *a = argv[i];
        const char *c1 = strchr(a, ':');
        if (strncmp(a, "cap=", 4) == 0) {
            if (parse_count(a + 4, &spec.capacity) != 0) return;
        } else if (strcmp(a, "ring") == 0) {
            spec.policy = TIKU_DB_RING;
        } else if (strcmp(a, "keep") == 0) {
            spec.policy = TIKU_DB_KEEP;
        } else if (strncmp(a, "rate=", 5) == 0) {
            if (parse_rate(a + 5, &spec.rate) != 0) {
                SHELL_PRINTF("db: rate is N/s, N/m, N/h or N/d\n");
                return;
            }
        } else if (strncmp(a, "life=", 5) == 0) {
            if (parse_life(a + 5, &spec.life) != 0) {
                SHELL_PRINTF("db: life is Nd or Ny\n");
                return;
            }
        } else if (c1 != NULL && ncols < TIKU_DB_COLS_MAX) {
            const char *ty = c1 + 1;
            const char *c2 = strchr(ty, ':');
            size_t nl = (size_t)(c1 - a), tl = c2 ? (size_t)(c2 - ty) : strlen(ty);
            int64_t size = 0;

            if (nl == 0u || nl >= TIKU_DB_COL_NAME_MAX) {
                SHELL_PRINTF("db: column name %s is 1 to %u characters\n", a,
                             (unsigned)(TIKU_DB_COL_NAME_MAX - 1));
                return;
            }
            memcpy(names[ncols], a, nl);
            names[ncols][nl] = '\0';
            cols[ncols].name = names[ncols];
            cols[ncols].size = 0;
            if (tl == 3u && strncmp(ty, "i32", 3) == 0) {
                cols[ncols].type = TIKU_DB_I32;
            } else if (tl == 3u && strncmp(ty, "u32", 3) == 0) {
                cols[ncols].type = TIKU_DB_U32;
            } else if (tl == 2u && strncmp(ty, "q3", 2) == 0) {
                cols[ncols].type = TIKU_DB_Q3;
            } else if (tl == 3u && strncmp(ty, "t64", 3) == 0) {
                cols[ncols].type = TIKU_DB_T64;
            } else if ((tl == 4u && strncmp(ty, "text", 4) == 0) ||
                       (tl == 5u && strncmp(ty, "bytes", 5) == 0)) {
                cols[ncols].type = (tl == 4u) ? TIKU_DB_TEXT : TIKU_DB_BYTES;
                if (c2 == NULL || parse_i64(c2 + 1, &size) != 0 || size < 1 ||
                    size > 255) {
                    SHELL_PRINTF("db: %s needs a size of 1 to 255, as "
                                 "<name>:text:<n> or <name>:bytes:<n>\n", a);
                    return;
                }
                cols[ncols].size = (uint8_t)size;
            } else {
                SHELL_PRINTF("db: %s: types are i32 u32 q3 t64 text:N bytes:N\n", a);
                return;
            }
            ncols++;
        } else {
            SHELL_PRINTF("db: %s is not a column, cap=, ring, keep, rate= or life=\n", a);
            return;
        }
    }
    rc = tiku_db_create(db, argv[2], cols, ncols, &spec, &needed);
    if (rc == TIKU_DB_ERR_LIFE) {
        SHELL_PRINTF("db: %lu rows wear out inside the life at that rate; "
                     "cap=%lu would last\n", (unsigned long)spec.capacity,
                     (unsigned long)needed);
    } else if (rc < 0) {
        SHELL_PRINTF("db: create %s: %s\n", argv[2], tiku_db_err_name(rc));
    } else {
        tiku_db_info_t info;
        (void)tiku_db_info(db, rc, &info);
        SHELL_PRINTF("created %s: %u columns, %lu rows of %u B\n", argv[2],
                     (unsigned)ncols, (unsigned long)info.capacity,
                     (unsigned)info.rec_size);
    }
}

/** @brief db insert <t> <v>...: one value per column, in schema order. */
static void verb_insert(tiku_db_t *db, int t, uint8_t argc, const char *argv[])
{
    tiku_db_val_t vals[TIKU_DB_COLS_MAX];
    tiku_db_info_t info;
    tiku_db_col_t c;
    uint32_t seq;
    size_t used = 0;
    uint8_t i;
    int rc;

    (void)tiku_db_info(db, t, &info);
    if ((uint8_t)(argc - 3u) != info.ncols) {
        SHELL_PRINTF("db: %s takes %u values\n", info.name, (unsigned)info.ncols);
        return;
    }
    for (i = 0; i < info.ncols; ++i) {
        (void)tiku_db_column(db, t, i, &c);
        if (parse_value(&c, argv[3 + i], &vals[i], scratch + used,
                        sizeof scratch - used) != 0) {
            return;
        }
        if (c.type == TIKU_DB_BYTES) {
            used += vals[i].len;
        }
    }
    rc = tiku_db_insert(db, t, vals, &seq);
    if (rc != TIKU_DB_OK) {
        SHELL_PRINTF("db: insert: %s\n", tiku_db_err_name(rc));
        return;
    }
    SHELL_PRINTF("seq %lu\n", (unsigned long)seq);
}

/** @brief db agg <t> <col> [<col> <op> <v>]: count min max sum mean. */
static void verb_agg(tiku_db_t *db, int t, uint8_t argc, const char *argv[])
{
    tiku_db_cursor_t cur;
    tiku_db_agg_t agg;
    tiku_db_col_t c;
    char a[24], b[24], s[24], m[24];
    int col = column(db, t, argv[3]);
    int rc;

    if (col < 0 || tiku_db_scan(db, t, &cur) != TIKU_DB_OK) {
        return;
    }
    if (argc == 7u &&
        add_filter(db, t, &cur, &argv[4], scratch, sizeof scratch) != 0) {
        return;
    }
    rc = tiku_db_agg(&cur, (uint8_t)col, &agg);
    if (rc != TIKU_DB_OK) {
        SHELL_PRINTF("db: agg %s: %s\n", argv[3], tiku_db_err_name(rc));
        return;
    }
    (void)tiku_db_column(db, t, (uint8_t)col, &c);
    if (c.type == TIKU_DB_Q3) {
        SHELL_PRINTF("count %lu min %s max %s sum %s mean %s\n",
                     (unsigned long)agg.count, q3_text(agg.min, a), q3_text(agg.max, b),
                     q3_text(agg.sum, s), q3_text(agg.mean, m));
    } else {
        SHELL_PRINTF("count %lu min %s max %s sum %s mean %s\n",
                     (unsigned long)agg.count, i64_text(agg.min, a), i64_text(agg.max, b),
                     i64_text(agg.sum, s), i64_text(agg.mean, m));
    }
}

/** @brief db delete <t> <seq> | db delete <t> where <col> <op> <v>. */
static void verb_delete(tiku_db_t *db, int t, uint8_t argc, const char *argv[])
{
    tiku_db_cursor_t cur;
    uint32_t seq, n;
    int rc;

    if (argc == 7u && strcmp(argv[3], "where") == 0) {
        if (tiku_db_scan(db, t, &cur) != TIKU_DB_OK ||
            add_filter(db, t, &cur, &argv[4], scratch, sizeof scratch) != 0) {
            return;
        }
        rc = tiku_db_delete_matching(&cur, &n);
        if (rc == TIKU_DB_OK) {
            SHELL_PRINTF("deleted %lu\n", (unsigned long)n);
        } else {
            SHELL_PRINTF("deleted %lu, then %s\n", (unsigned long)n,
                         tiku_db_err_name(rc));
        }
        return;
    }
    if (argc != 4u || parse_count(argv[3], &seq) != 0) {
        SHELL_PRINTF("Usage: db delete <t> <seq> | db delete <t> where <col> <op> <v>\n");
        return;
    }
    rc = tiku_db_delete(db, t, seq);
    if (rc == TIKU_DB_OK) {
        SHELL_PRINTF("deleted 1\n");
    } else {
        SHELL_PRINTF("db: delete: %s\n", tiku_db_err_name(rc));
    }
}

/** @brief Print one [BM] line for @p n samples of db.@p name.@p rec "b". */
static void bench_line(const char *name, uint16_t rec, uint32_t *s, uint32_t n)
{
    uint32_t i, j;
    uint64_t sum = 0;

    for (i = 1; i < n; ++i) {                 /* insertion sort, n is small */
        uint32_t v = s[i];
        for (j = i; j > 0 && s[j - 1] > v; --j) {
            s[j] = s[j - 1];
        }
        s[j] = v;
    }
    for (i = 0; i < n; ++i) {
        sum += s[i];
    }
    SHELL_PRINTF("[BM] name=db.%s.%ub unit=%s direction=lower clock=%s hz=%lu "
                 "resolution=%lu n=%lu warmup=1 min=%lu mean=%lu p50=%lu p95=%lu "
                 "p99=%lu max=%lu samples=", name, (unsigned)rec, tiku_bench_unit(),
                 tiku_bench_clock(), (unsigned long)tiku_bench_hz(),
                 (unsigned long)tiku_bench_resolution(), (unsigned long)n,
                 (unsigned long)s[0], (unsigned long)(sum / n),
                 (unsigned long)s[(n - 1u) * 50u / 100u],
                 (unsigned long)s[(n - 1u) * 95u / 100u],
                 (unsigned long)s[(n - 1u) * 99u / 100u], (unsigned long)s[n - 1u]);
    for (i = 0; i < n; ++i) {
        SHELL_PRINTF(i ? ",%lu" : "%lu", (unsigned long)s[i]);
    }
    SHELL_PRINTF("\n");
}

#define BENCH_MAX  32u

/**
 * @brief db bench [n] [pad]: time insert, get, last 10 and an aggregate on a
 *        scratch table whose records carry @p pad more bytes, then drop it.
 *
 * Each line's name carries the record's size in bytes.
 */
static void verb_bench(tiku_db_t *db, uint8_t argc, const char *argv[])
{
    tiku_db_coldef_t cols[4] = {
        { "t", TIKU_DB_T64, 0 }, { "v", TIKU_DB_Q3, 0 }, { "src", TIKU_DB_I32, 0 },
        { "pad", TIKU_DB_BYTES, 0 },
    };
    uint32_t s[BENCH_MAX], n = 16, pad = 0, i, seq = 0;
    tiku_db_val_t vals[4];
    tiku_db_spec_t spec;
    tiku_db_cursor_t cur;
    tiku_db_info_t info;
    tiku_db_row_t row;
    tiku_db_agg_t agg;
    int t;

    if (argc >= 3u && (parse_count(argv[2], &n) != 0 || n > BENCH_MAX)) {
        SHELL_PRINTF("db: bench takes 1 to %u samples\n", (unsigned)BENCH_MAX);
        return;
    }
    if (argc >= 4u && (parse_count(argv[3], &pad) != 0 || pad > 200u)) {
        SHELL_PRINTF("db: bench pads a record by 1 to 200 bytes\n");
        return;
    }
    cols[3].size = (uint8_t)pad;
    memset(&spec, 0, sizeof spec);
    spec.capacity = 64;
    t = tiku_db_create(db, "_bench", cols, pad ? 4u : 3u, &spec, NULL);
    if (t < 0) {
        SHELL_PRINTF("db: bench table: %s\n", tiku_db_err_name(t));
        return;
    }
    (void)tiku_db_info(db, t, &info);
    memset(vals, 0, sizeof vals);
    tiku_bench_init();
    (void)tiku_db_insert(db, t, vals, &seq);           /* warmup */
    for (i = 0; i < n; ++i) {
        tiku_bench_time_t a;
        vals[0].n = (int64_t)i;
        vals[1].n = (int64_t)i * 1000;
        a = tiku_bench_now();
        (void)tiku_db_insert(db, t, vals, &seq);
        s[i] = tiku_bench_delta(a, tiku_bench_now());
    }
    bench_line("insert", info.rec_size, s, n);
    for (i = 0; i < n; ++i) {
        tiku_bench_time_t a = tiku_bench_now();
        (void)tiku_db_get(db, t, seq, &row);
        s[i] = tiku_bench_delta(a, tiku_bench_now());
    }
    bench_line("get", info.rec_size, s, n);
    for (i = 0; i < n; ++i) {
        tiku_bench_time_t a = tiku_bench_now();
        (void)tiku_db_last(db, t, 10, &cur);
        while (tiku_db_next(&cur, &row) == 1) {
        }
        s[i] = tiku_bench_delta(a, tiku_bench_now());
    }
    bench_line("last10", info.rec_size, s, n);
    for (i = 0; i < n; ++i) {
        tiku_bench_time_t a = tiku_bench_now();
        (void)tiku_db_scan(db, t, &cur);
        (void)tiku_db_agg(&cur, 1, &agg);
        s[i] = tiku_bench_delta(a, tiku_bench_now());
    }
    bench_line("agg", info.rec_size, s, n);
    (void)tiku_db_drop(db, t);
}

/*---------------------------------------------------------------------------*/
/* COMMAND                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Print the verbs. */
static void usage(void)
{
    SHELL_PRINTF("Usage: db [status|tables|format --erase|bench [n] [pad]]\n"
                 "  db create <t> <col:type[:n]>... [cap=N] [ring|keep] "
                 "[rate=N/s|m|h|d] [life=Nd|Ny]\n"
                 "  db insert <t> <v>...      db last <t> [n]    db scan <t> [n]\n"
                 "  db where <t> <col> <op> <v> [last <n>]\n"
                 "  db range <t> <col> <lo> <hi>   db agg <t> <col> [<col> <op> <v>]\n"
                 "  db delete <t> <seq> | db delete <t> where <col> <op> <v>\n"
                 "  db begin|commit|abort|schema|stat|drop <t>\n"
                 "types: i32 u32 q3 t64 text:N bytes:N; ops: = != < <= > >=\n");
}

void tiku_shell_cmd_db(uint8_t argc, const char *argv[])
{
    const char *verb = (argc >= 2u) ? argv[1] : "status";
    tiku_db_cursor_t cur;
    uint32_t n;
    tiku_db_t *db;
    int t, rc;

    if (strcmp(verb, "status") == 0) {
        verb_status();
        return;
    }
    if (strcmp(verb, "help") == 0) {
        usage();
        return;
    }
    if (strcmp(verb, "format") == 0) {
        if (argc != 3u || strcmp(argv[2], "--erase") != 0) {
            SHELL_PRINTF("db: format erases every table; type db format --erase\n");
            return;
        }
        rc = tiku_db_board_format();
        SHELL_PRINTF("db format: %s\n", tiku_db_err_name(rc));
        return;
    }
    db = board();
    if (db == NULL) {
        return;
    }
    if (strcmp(verb, "tables") == 0) {
        verb_tables(db);
        return;
    }
    if (strcmp(verb, "bench") == 0) {
        verb_bench(db, argc, argv);
        return;
    }
    if (argc < 3u) {
        usage();
        return;
    }
    if (strcmp(verb, "create") == 0) {
        verb_create(db, argc, argv);
        return;
    }
    t = table(db, argv[2]);
    if (t < 0) {
        return;
    }
    if (strcmp(verb, "insert") == 0) {
        verb_insert(db, t, argc, argv);
    } else if (strcmp(verb, "last") == 0 || strcmp(verb, "scan") == 0) {
        n = 10;
        if (argc >= 4u && parse_count(argv[3], &n) != 0) {
            return;
        }
        if (verb[0] == 'l') {
            (void)tiku_db_last(db, t, n, &cur);
        } else {
            (void)tiku_db_scan(db, t, &cur);
            (void)tiku_db_limit(&cur, n);
        }
        print_rows(db, t, &cur);
    } else if (strcmp(verb, "where") == 0 && (argc == 6u || argc == 8u)) {
        if (argc == 8u) {
            if (strcmp(argv[6], "last") != 0 || parse_count(argv[7], &n) != 0) {
                usage();
                return;
            }
            (void)tiku_db_last(db, t, 0xFFFFFFFFu, &cur);
            (void)tiku_db_limit(&cur, n);
        } else {
            (void)tiku_db_scan(db, t, &cur);
        }
        if (add_filter(db, t, &cur, &argv[3], scratch, sizeof scratch) == 0) {
            print_rows(db, t, &cur);
        }
    } else if (strcmp(verb, "range") == 0 && argc == 6u) {
        const char *lo[3] = { argv[3], ">=", argv[4] };
        const char *hi[3] = { argv[3], "<=", argv[5] };
        (void)tiku_db_scan(db, t, &cur);
        if (add_filter(db, t, &cur, lo, scratch, sizeof scratch / 2u) == 0 &&
            add_filter(db, t, &cur, hi, scratch + sizeof scratch / 2u,
                       sizeof scratch / 2u) == 0) {
            print_rows(db, t, &cur);
        }
    } else if (strcmp(verb, "agg") == 0 && (argc == 4u || argc == 7u)) {
        verb_agg(db, t, argc, argv);
    } else if (strcmp(verb, "delete") == 0) {
        verb_delete(db, t, argc, argv);
    } else if (strcmp(verb, "begin") == 0) {
        rc = tiku_db_begin(db, t);
        SHELL_PRINTF("begin: %s\n", tiku_db_err_name(rc));
    } else if (strcmp(verb, "commit") == 0) {
        rc = tiku_db_commit(db, t);
        SHELL_PRINTF("commit: %s\n", tiku_db_err_name(rc));
    } else if (strcmp(verb, "abort") == 0) {
        rc = tiku_db_abort(db, t);
        SHELL_PRINTF("abort: %s\n", tiku_db_err_name(rc));
    } else if (strcmp(verb, "schema") == 0) {
        verb_schema(db, t);
    } else if (strcmp(verb, "stat") == 0) {
        verb_stat(db, t);
    } else if (strcmp(verb, "drop") == 0) {
        rc = tiku_db_drop(db, t);
        SHELL_PRINTF("drop: %s\n", tiku_db_err_name(rc));
    } else {
        usage();
    }
}

#endif /* TIKU_SHELL_CMD_DB */
