/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mailbox.c - the mailbox's layout on the part and its operations.
 *
 * A superblock, a one-record journal, a table of entries, a read mark per
 * entry, a bitmap of data blocks and the blocks.  A file's data goes into
 * free blocks before its entry commits, so a cut leaves the old or the new.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mailbox.h"
#include <kernel/vfs/tiku_vfs.h>            /* TIKU_VFS_* statuses       */
#include <kernel/memory/tiku_nvm_mirror.h>  /* tiku_nvm_crc32_update     */
#include <limits.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* LAYOUT                                                                    */
/*---------------------------------------------------------------------------*/
/*
 * Offset 0: superblock (32 bytes).  32: journal (16).  64: the table, an
 * entry of 48 bytes per file.  Then the marks, a byte per entry, 1 once the
 * processor that did not write the file has read it to its end; the bitmap,
 * from a 16-byte boundary, a bit per block, set while a block may hold
 * data; the blocks, from a 64-byte boundary.  Values are little-endian.
 */

#define MB_MAGIC        0x424D4B54UL     /* "TKMB" */
#define MB_VERSION      2u
#define MB_SUPER_LEN    32u
#define MB_JOURNAL      32u
#define MB_JOURNAL_LEN  16u
#define MB_JOURNAL_TAG  0x4A4Du          /* "MJ" */
#define MB_TABLE        64u
#define MB_ENTRY        48u
#define MB_CHUNK        32u              /* bytes moved at a time */

/* Entry fields: a CRC of bytes 4..47 that is never 0 or all ones (0 marks
 * a deleted entry, all ones one never used), the sequence number, one more
 * than any entry before it, the first block, the length, the data's CRC,
 * the writer's side and the NUL-padded name. */
#define E_CRC    0u
#define E_SEQ    4u
#define E_START  8u
#define E_LEN    12u
#define E_DCRC   16u
#define E_WRITER 20u
#define E_NAME   24u
#define KEEP_LEN 5u                       /* strlen(TIKU_MAILBOX_KEEP) */

/* Slot states. */
#define SLOT_NEVER  0   /* never written: it and every later slot */
#define SLOT_FREE   1   /* deleted, or a torn write */
#define SLOT_VALID  2

/** @brief Where the marks, the bitmap and the blocks are on a part. */
typedef struct {
    uint32_t marks;
    uint32_t bitmap;
    uint32_t data;
    uint32_t blocks;
} geo_t;

/** @brief An entry as read. */
typedef struct {
    uint32_t seq, start, len, dcrc;
    uint8_t  writer;
    char     name[TIKU_MAILBOX_NAME_MAX];
} entry_t;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Read a 32-bit little-endian value. */
static uint32_t
get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/** @brief Store a 32-bit little-endian value. */
static void
put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/** @brief CRC-32 of @p len bytes. */
static uint32_t
mb_crc32(const uint8_t *p, uint32_t len)
{
    return tiku_nvm_crc32_update(0xFFFFFFFFUL, p, (size_t)len) ^ 0xFFFFFFFFUL;
}

/** @brief The CRC an entry's first word holds: never 0 or all ones. */
static uint32_t
entry_crc(const uint8_t *e)
{
    uint32_t c = mb_crc32(e + 4, MB_ENTRY - 4u);

    return (c == 0UL || c == 0xFFFFFFFFUL) ? 1UL : c;
}

/** @brief The layout of a part of @p size bytes; blocks 0 when too small. */
static geo_t
geo_of(uint32_t size)
{
    geo_t g;
    uint32_t guess, bytes;

    g.marks = MB_TABLE + (uint32_t)TIKU_MAILBOX_FILES * MB_ENTRY;
    g.bitmap = (g.marks + TIKU_MAILBOX_FILES + 15UL) & ~(uint32_t)15u;
    g.data = 0UL;
    g.blocks = 0UL;
    if (size <= g.bitmap + TIKU_MAILBOX_BLOCK) {
        return g;
    }
    guess = (size - g.bitmap) / TIKU_MAILBOX_BLOCK;
    bytes = (guess + 7UL) / 8UL;
    g.data = (g.bitmap + bytes + TIKU_MAILBOX_BLOCK - 1UL) &
             ~(uint32_t)(TIKU_MAILBOX_BLOCK - 1u);
    if (g.data < size) {
        g.blocks = (size - g.data) / TIKU_MAILBOX_BLOCK;
    }
    return g;
}

/** @brief Read @p len bytes; TIKU_VFS_EIO when the medium fails. */
static int
rd(const tiku_mailbox_medium_t *m, uint32_t a, uint8_t *b, uint32_t len)
{
    return (m->read(m->ctx, a, b, len) == 0) ? 0 : TIKU_VFS_EIO;
}

/** @brief Write @p len bytes; TIKU_VFS_EIO when the medium fails. */
static int
wr(const tiku_mailbox_medium_t *m, uint32_t a, const uint8_t *b,
   uint32_t len)
{
    return (m->write(m->ctx, a, b, len) == 0) ? 0 : TIKU_VFS_EIO;
}

/** @brief 1 for a name under keep/. */
static int
is_kept(const char *name)
{
    return strncmp(name, TIKU_MAILBOX_KEEP, KEEP_LEN) == 0;
}

/** @brief 1 for a name of 1 to 23 printable characters without a space,
 *         with no '/' but the one keep/ may start it with. */
static int
name_ok(const char *name)
{
    size_t i, n, from;

    if (name == (const char *)0) {
        return 0;
    }
    n = strlen(name);
    from = is_kept(name) ? KEEP_LEN : 0u;
    if (n == from || n >= TIKU_MAILBOX_NAME_MAX) {
        return 0;
    }
    for (i = from; i < n; i++) {
        if (name[i] <= ' ' || name[i] > '~' || name[i] == '/') {
            return 0;
        }
    }
    return 1;
}

/** @brief Address of table slot @p s. */
static uint32_t
slot_addr(uint8_t s)
{
    return MB_TABLE + (uint32_t)s * MB_ENTRY;
}

/**
 * @brief Read slot @p s and say what it holds.
 * @return SLOT_*, with @p e filled for SLOT_VALID, or TIKU_VFS_EIO
 */
static int
slot_read(const tiku_mailbox_medium_t *m, uint8_t s, entry_t *e)
{
    uint8_t b[MB_ENTRY];
    uint32_t c;

    if (rd(m, slot_addr(s), b, MB_ENTRY) != 0) {
        return TIKU_VFS_EIO;
    }
    c = get32(b + E_CRC);
    if (c == 0xFFFFFFFFUL) {
        return SLOT_NEVER;
    }
    if (c == 0UL || c != entry_crc(b)) {
        return SLOT_FREE;
    }
    e->seq = get32(b + E_SEQ);
    e->start = get32(b + E_START);
    e->len = get32(b + E_LEN);
    e->dcrc = get32(b + E_DCRC);
    e->writer = b[E_WRITER];
    memcpy(e->name, b + E_NAME, TIKU_MAILBOX_NAME_MAX);
    e->name[TIKU_MAILBOX_NAME_MAX - 1u] = '\0';
    return SLOT_VALID;
}

/** @brief Write @p e into slot @p s, CRC first so a torn write fails it. */
static int
slot_write(const tiku_mailbox_medium_t *m, uint8_t s, const entry_t *e)
{
    uint8_t b[MB_ENTRY];

    memset(b, 0, sizeof(b));
    put32(b + E_SEQ, e->seq);
    put32(b + E_START, e->start);
    put32(b + E_LEN, e->len);
    put32(b + E_DCRC, e->dcrc);
    b[E_WRITER] = e->writer;
    memcpy(b + E_NAME, e->name, strlen(e->name));
    put32(b + E_CRC, entry_crc(b));
    return wr(m, slot_addr(s), b, MB_ENTRY);
}

/** @brief Mark slot @p s deleted: zero its CRC word. */
static int
slot_clear(const tiku_mailbox_medium_t *m, uint8_t s)
{
    static const uint8_t zero[4] = { 0u, 0u, 0u, 0u };

    return wr(m, slot_addr(s) + E_CRC, zero, 4u);
}

/** @brief Read slot @p s's mark into @p v. */
static int
mark_get(const tiku_mailbox_medium_t *m, const geo_t *g, uint8_t s,
         uint8_t *v)
{
    return rd(m, g->marks + s, v, 1u);
}

/** @brief Set slot @p s's mark to @p v, in one byte. */
static int
mark_put(const tiku_mailbox_medium_t *m, const geo_t *g, uint8_t s,
         uint8_t v)
{
    return wr(m, g->marks + s, &v, 1u);
}

/** @brief Blocks a file of @p len bytes takes. */
static uint32_t
blocks_for(uint32_t len)
{
    return (len + TIKU_MAILBOX_BLOCK - 1UL) / TIKU_MAILBOX_BLOCK;
}

/*---------------------------------------------------------------------------*/
/* THE BITMAP                                                                */
/*---------------------------------------------------------------------------*/
/*
 * A bit is set before its block takes data and cleared only after no entry
 * names the block, so the set bits always cover every file: a cut can leave
 * a block marked that no file holds, never the reverse.
 */

/** @brief Set or clear the bits of @p count blocks from @p start. */
static int
bits_put(const tiku_mailbox_medium_t *m, const geo_t *g, uint32_t start,
         uint32_t count, uint8_t on)
{
    uint32_t b = start, end = start + count;

    while (b < end) {
        uint32_t byte = b / 8UL;
        uint8_t v;
        int rc = rd(m, g->bitmap + byte, &v, 1u);

        if (rc != 0) {
            return rc;
        }
        while (b < end && b / 8UL == byte) {
            uint8_t bit = (uint8_t)(1u << (b % 8UL));
            v = on ? (uint8_t)(v | bit) : (uint8_t)(v & (uint8_t)~bit);
            b++;
        }
        rc = wr(m, g->bitmap + byte, &v, 1u);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/**
 * @brief Walk the bitmap: find the first run of @p need clear bits, count
 *        the clear bits and the longest clear run.
 * @return 0 and *at set when a run was found, TIKU_VFS_E2BIG when none,
 *         TIKU_VFS_EIO
 */
static int
bits_scan(const tiku_mailbox_medium_t *m, const geo_t *g, uint32_t need,
          uint32_t *at, uint32_t *clear, uint32_t *longest)
{
    uint8_t buf[MB_CHUNK];
    uint32_t b, run = 0UL, run_at = 0UL, found = 0UL, cl = 0UL, lg = 0UL;
    uint8_t have = 0u;

    memset(buf, 0, sizeof(buf));

    for (b = 0UL; b < g->blocks; b++) {
        uint32_t i = (b / 8UL) % MB_CHUNK;

        if (b % (8UL * MB_CHUNK) == 0UL) {
            uint32_t left = (g->blocks - b + 7UL) / 8UL;
            int rc = rd(m, g->bitmap + b / 8UL, buf,
                        left < MB_CHUNK ? left : MB_CHUNK);
            if (rc != 0) {
                return rc;
            }
        }
        if ((buf[i] & (uint8_t)(1u << (b % 8UL))) != 0u) {
            run = 0UL;
            continue;
        }
        if (run == 0UL) {
            run_at = b;
        }
        run++;
        cl++;
        if (run > lg) {
            lg = run;
        }
        if (!have && need != 0UL && run == need) {
            found = run_at;
            have = 1u;
        }
    }
    if (clear != (uint32_t *)0) {
        *clear = cl;
    }
    if (longest != (uint32_t *)0) {
        *longest = lg;
    }
    if (need == 0UL) {
        *at = 0UL;
        return 0;
    }
    if (!have) {
        return TIKU_VFS_E2BIG;
    }
    *at = found;
    return 0;
}

/**
 * @brief Rewrite the bitmap from the table, a window of bytes at a time;
 *        each byte written still covers every file, so a cut is harmless.
 * @param freed  Out: bits that were set and that no file holds
 */
static int
bits_rebuild(const tiku_mailbox_medium_t *m, const geo_t *g,
             uint32_t *freed)
{
    uint32_t bytes = (g->blocks + 7UL) / 8UL, w, n, i;
    uint8_t want[MB_CHUNK], had[MB_CHUNK];
    entry_t e;

    *freed = 0UL;
    for (w = 0UL; w < bytes; w += MB_CHUNK) {
        uint32_t lo = w * 8UL, hi;
        int s, rc;

        n = (bytes - w < MB_CHUNK) ? bytes - w : MB_CHUNK;
        hi = lo + n * 8UL;
        memset(want, 0, sizeof(want));
        for (s = 0; s < (int)TIKU_MAILBOX_FILES; s++) {
            uint32_t b, end;

            rc = slot_read(m, (uint8_t)s, &e);
            if (rc < 0) {
                return rc;
            }
            if (rc == SLOT_NEVER) {
                break;
            }
            if (rc != SLOT_VALID) {
                continue;
            }
            end = e.start + blocks_for(e.len);
            for (b = (e.start > lo) ? e.start : lo; b < end && b < hi; b++) {
                want[(b - lo) / 8UL] |= (uint8_t)(1u << (b % 8UL));
            }
        }
        rc = rd(m, g->bitmap + w, had, n);
        if (rc != 0) {
            return rc;
        }
        for (i = 0UL; i < n; i++) {
            uint8_t gone = (uint8_t)(had[i] & (uint8_t)~want[i]);

            while (gone != 0u) {
                *freed += gone & 1u;
                gone >>= 1;
            }
        }
        if (memcmp(want, had, (size_t)n) != 0) {
            rc = wr(m, g->bitmap + w, want, n);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SUPERBLOCK AND JOURNAL                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Check the superblock against the part's layout.
 * @return 0 with @p g filled, TIKU_VFS_ENOTSUP when the part holds no
 *         mailbox, TIKU_VFS_EIO
 */
static int
super_load(const tiku_mailbox_medium_t *m, geo_t *g)
{
    uint8_t b[MB_SUPER_LEN];

    if (rd(m, 0UL, b, MB_SUPER_LEN) != 0) {
        return TIKU_VFS_EIO;
    }
    *g = geo_of(m->size);
    if (get32(b) != MB_MAGIC || get32(b + 28) != mb_crc32(b, 28u) ||
        (uint16_t)(b[4] | (b[5] << 8)) != MB_VERSION ||
        (uint16_t)(b[6] | (b[7] << 8)) != TIKU_MAILBOX_FILES ||
        get32(b + 8) != m->size || get32(b + 12) != g->bitmap ||
        get32(b + 16) != g->data || get32(b + 20) != g->blocks ||
        get32(b + 24) != g->marks || g->blocks == 0UL) {
        return TIKU_VFS_ENOTSUP;
    }
    return 0;
}

/** @brief Write the journal: slot @p from is being replaced by slot @p to,
 *         whose entry carries @p seq; @p set 0 clears it. */
static int
journal_put(const tiku_mailbox_medium_t *m, uint8_t set, uint8_t from,
            uint8_t to, uint32_t seq)
{
    uint8_t j[MB_JOURNAL_LEN];

    memset(j, 0, sizeof(j));
    if (set) {
        j[0] = from;
        j[1] = to;
        j[2] = (uint8_t)MB_JOURNAL_TAG;
        j[3] = (uint8_t)(MB_JOURNAL_TAG >> 8);
        put32(j + 4, seq);
        put32(j + 12, mb_crc32(j, 12u));
    }
    return wr(m, MB_JOURNAL, j, MB_JOURNAL_LEN);
}

/**
 * @brief Finish a replacement a cut stopped: when the new entry is whole,
 *        delete the old one and free its blocks; then clear the journal.
 */
static int
journal_recover(const tiku_mailbox_medium_t *m, const geo_t *g)
{
    uint8_t j[MB_JOURNAL_LEN], b[MB_ENTRY];
    entry_t e;
    int rc;

    if ((rc = rd(m, MB_JOURNAL, j, MB_JOURNAL_LEN)) != 0) {
        return rc;
    }
    if ((uint16_t)(j[2] | (j[3] << 8)) != MB_JOURNAL_TAG ||
        get32(j + 12) != mb_crc32(j, 12u) || j[0] >= TIKU_MAILBOX_FILES ||
        j[1] >= TIKU_MAILBOX_FILES) {
        return 0;
    }
    rc = slot_read(m, j[1], &e);
    if (rc < 0) {
        return rc;
    }
    if (rc == SLOT_VALID && e.seq == get32(j + 4)) {
        /* The old entry's fields outlive its CRC word, so its blocks are
         * known even when the cut came after that word was cleared. */
        if ((rc = rd(m, slot_addr(j[0]), b, MB_ENTRY)) != 0 ||
            (rc = slot_clear(m, j[0])) != 0 ||
            (rc = bits_put(m, g, get32(b + E_START),
                           blocks_for(get32(b + E_LEN)), 0u)) != 0) {
            return rc;
        }
    }
    return journal_put(m, 0u, 0u, 0u, 0UL);
}

/*---------------------------------------------------------------------------*/
/* AN OPERATION                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Take the bus, check the superblock and finish a cut replacement. */
static int
op_begin(const tiku_mailbox_medium_t *m, geo_t *g)
{
    int rc;

    if (m->take != 0 && m->take(m->ctx) != 0) {
        return TIKU_VFS_EBUSY;
    }
    rc = super_load(m, g);
    if (rc == 0) {
        rc = journal_recover(m, g);
    }
    if (rc != 0 && m->give != 0) {
        m->give(m->ctx);
    }
    return rc;
}

/** @brief Give the bus back and pass @p rc on. */
static int
op_end(const tiku_mailbox_medium_t *m, int rc)
{
    if (m->give != 0) {
        m->give(m->ctx);
    }
    return rc;
}

/**
 * @brief Find the file @p name, the first slot a new entry may take and the
 *        highest sequence number in the table.
 * @param slot  Out: the file's slot, -1 when there is none
 * @param room  Out: a free or never-used slot, -1 when the table is full
 * @param top   Out, optional: the highest sequence number, 0 for none
 */
static int
find(const tiku_mailbox_medium_t *m, const char *name, int *slot,
     entry_t *e, int *room, uint32_t *top)
{
    int s, rc;
    entry_t t;

    *slot = -1;
    *room = -1;
    if (top != (uint32_t *)0) {
        *top = 0UL;
    }
    for (s = 0; s < (int)TIKU_MAILBOX_FILES; s++) {
        rc = slot_read(m, (uint8_t)s, &t);
        if (rc < 0) {
            return rc;
        }
        if (rc != SLOT_VALID) {
            if (*room < 0) {
                *room = s;
            }
            if (rc == SLOT_NEVER) {
                break;
            }
            continue;
        }
        if (top != (uint32_t *)0 && t.seq > *top) {
            *top = t.seq;
        }
        if (*slot < 0 && strcmp(t.name, name) == 0) {
            *slot = s;
            *e = t;
        }
    }
    return 0;
}

/**
 * @brief Reclaim the oldest file that is read and not under keep/, other
 *        than slot @p spare: delete it as tiku_mailbox_delete() does.
 * @return its slot, -1 when no file is reclaimable, or a negative status
 *         below -1
 */
static int
reclaim_one(const tiku_mailbox_medium_t *m, const geo_t *g, int spare)
{
    int s, rc, oldest = -1;
    uint32_t seq = 0UL;
    entry_t e, o;
    uint8_t mark;

    memset(&o, 0, sizeof(o));

    for (s = 0; s < (int)TIKU_MAILBOX_FILES; s++) {
        rc = slot_read(m, (uint8_t)s, &e);
        if (rc < 0) {
            return rc;
        }
        if (rc == SLOT_NEVER) {
            break;
        }
        if (rc != SLOT_VALID || s == spare || is_kept(e.name) ||
            (oldest >= 0 && e.seq >= seq)) {
            continue;
        }
        if ((rc = mark_get(m, g, (uint8_t)s, &mark)) != 0) {
            return rc;
        }
        if (mark == 1u) {
            oldest = s;
            seq = e.seq;
            o = e;
        }
    }
    if (oldest < 0) {
        return -1;
    }
    if ((rc = slot_clear(m, (uint8_t)oldest)) != 0 ||
        (rc = bits_put(m, g, o.start, blocks_for(o.len), 0u)) != 0) {
        return rc;
    }
    return oldest;
}

/*---------------------------------------------------------------------------*/
/* CALLS                                                                     */
/*---------------------------------------------------------------------------*/

int
tiku_mailbox_format(const tiku_mailbox_medium_t *m)
{
    uint8_t b[MB_CHUNK];
    geo_t g = geo_of(m->size);
    uint32_t a, end;
    int rc = 0;

    if (g.blocks == 0UL) {
        return TIKU_VFS_E2BIG;
    }
    if (m->take != 0 && m->take(m->ctx) != 0) {
        return TIKU_VFS_EBUSY;
    }
    /* The superblock goes last: a cut format is no mailbox. */
    memset(b, 0, sizeof(b));
    rc = wr(m, 0UL, b, MB_SUPER_LEN);
    if (rc == 0) {
        rc = journal_put(m, 0u, 0u, 0u, 0UL);
    }
    memset(b, 0xFF, sizeof(b));
    for (a = MB_TABLE; rc == 0 && a < g.marks; a += MB_CHUNK) {
        rc = wr(m, a, b, (g.marks - a < MB_CHUNK) ? g.marks - a : MB_CHUNK);
    }
    memset(b, 0, sizeof(b));
    end = g.bitmap + (g.blocks + 7UL) / 8UL;
    for (a = g.marks; rc == 0 && a < end; a += MB_CHUNK) {
        rc = wr(m, a, b, (end - a < MB_CHUNK) ? end - a : MB_CHUNK);
    }
    if (rc == 0) {
        put32(b, MB_MAGIC);
        b[4] = (uint8_t)MB_VERSION;
        b[5] = 0u;
        b[6] = (uint8_t)TIKU_MAILBOX_FILES;
        b[7] = (uint8_t)(TIKU_MAILBOX_FILES >> 8);
        put32(b + 8, m->size);
        put32(b + 12, g.bitmap);
        put32(b + 16, g.data);
        put32(b + 20, g.blocks);
        put32(b + 24, g.marks);
        put32(b + 28, mb_crc32(b, 28u));
        rc = wr(m, 0UL, b, MB_SUPER_LEN);
    }
    return op_end(m, rc);
}

int
tiku_mailbox_write(const tiku_mailbox_medium_t *m, const char *name,
                   const uint8_t *data, uint32_t len)
{
    geo_t g;
    entry_t old, e;
    int slot, room, rc, rebuilt = 0, reclaims = 0;
    uint32_t need = blocks_for(len), at = 0UL, done, freed, top;

    if (!name_ok(name)) {
        return TIKU_VFS_EINVAL;
    }
    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    if ((rc = find(m, name, &slot, &old, &room, &top)) != 0) {
        return op_end(m, rc);
    }
    /* Room for the entry and the data: first blocks a cut left marked,
     * then read files outside keep/, the oldest first.  Each reclaim
     * removes another file, so a medium that drops writes cannot hold the
     * loop past one reclaim per entry. */
    for (;;) {
        if (reclaims > (int)TIKU_MAILBOX_FILES) {
            return op_end(m, TIKU_VFS_EIO);
        }
        if (room < 0) {
            reclaims++;
            rc = reclaim_one(m, &g, slot);
            if (rc < -1) {
                return op_end(m, rc);
            }
            if (rc < 0) {
                return op_end(m, TIKU_VFS_E2BIG);   /* table full */
            }
            room = rc;
            continue;
        }
        rc = bits_scan(m, &g, need, &at, (uint32_t *)0, (uint32_t *)0);
        if (rc != TIKU_VFS_E2BIG) {
            break;
        }
        if (!rebuilt) {
            rebuilt = 1;
            rc = bits_rebuild(m, &g, &freed);
        } else {
            reclaims++;
            rc = reclaim_one(m, &g, slot);
            if (rc == -1) {
                rc = TIKU_VFS_E2BIG;
            } else if (rc >= 0) {
                rc = 0;
            }
        }
        if (rc != 0) {
            return op_end(m, rc);
        }
    }
    if (rc != 0) {
        return op_end(m, rc);
    }
    if ((rc = bits_put(m, &g, at, need, 1u)) != 0) {
        return op_end(m, rc);
    }
    for (done = 0UL; done < len; done += MB_CHUNK) {
        uint32_t n = (len - done < MB_CHUNK) ? len - done : MB_CHUNK;

        rc = wr(m, g.data + at * TIKU_MAILBOX_BLOCK + done, data + done, n);
        if (rc != 0) {
            return op_end(m, rc);
        }
    }
    memset(&e, 0, sizeof(e));
    e.seq = top + 1UL;
    e.start = at;
    e.len = len;
    e.dcrc = mb_crc32(data, len);
    e.writer = m->side;
    strcpy(e.name, name);
    if (slot >= 0 &&
        (rc = journal_put(m, 1u, (uint8_t)slot, (uint8_t)room, e.seq)) != 0) {
        return op_end(m, rc);
    }
    /* The slot's mark is cleared before its entry exists, so new content
     * is never taken for read. */
    if ((rc = mark_put(m, &g, (uint8_t)room, 0u)) != 0 ||
        (rc = slot_write(m, (uint8_t)room, &e)) != 0) {
        return op_end(m, rc);
    }
    if (slot >= 0) {
        if ((rc = slot_clear(m, (uint8_t)slot)) == 0 &&
            (rc = bits_put(m, &g, old.start, blocks_for(old.len), 0u)) == 0) {
            rc = journal_put(m, 0u, 0u, 0u, 0UL);
        }
    }
    return op_end(m, rc);
}

int
tiku_mailbox_read_at(const tiku_mailbox_medium_t *m, const char *name,
                     uint32_t offset, uint8_t *buf, uint32_t max,
                     uint32_t *total)
{
    geo_t g;
    entry_t e;
    int slot, room, rc;
    uint32_t n = 0UL;
    uint8_t mark;

    if (!name_ok(name)) {
        return TIKU_VFS_ENOENT;
    }
    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    if ((rc = find(m, name, &slot, &e, &room, (uint32_t *)0)) != 0) {
        return op_end(m, rc);
    }
    if (slot < 0) {
        return op_end(m, TIKU_VFS_ENOENT);
    }
    if (total != (uint32_t *)0) {
        *total = e.len;
    }
    if (offset < e.len) {
        n = e.len - offset;
        if (n > max) {
            n = max;
        }
        if (n > (uint32_t)INT_MAX) {
            n = (uint32_t)INT_MAX;               /* the count fits the int */
        }
        rc = rd(m, g.data + e.start * TIKU_MAILBOX_BLOCK + offset, buf, n);
    }
    /* The other processor has now read up to the end. */
    if (rc == 0 && e.writer != m->side && offset + n >= e.len &&
        (rc = mark_get(m, &g, (uint8_t)slot, &mark)) == 0 && mark != 1u) {
        rc = mark_put(m, &g, (uint8_t)slot, 1u);
    }
    return op_end(m, (rc != 0) ? rc : (int)n);
}

int
tiku_mailbox_stat(const tiku_mailbox_medium_t *m, const char *name,
                  uint32_t *len)
{
    geo_t g;
    entry_t e;
    int slot, room, rc;

    if (!name_ok(name)) {
        return TIKU_VFS_ENOENT;
    }
    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    if ((rc = find(m, name, &slot, &e, &room, (uint32_t *)0)) != 0) {
        return op_end(m, rc);
    }
    if (slot < 0) {
        return op_end(m, TIKU_VFS_ENOENT);
    }
    if (len != (uint32_t *)0) {
        *len = e.len;
    }
    return op_end(m, 0);
}

int
tiku_mailbox_delete(const tiku_mailbox_medium_t *m, const char *name)
{
    geo_t g;
    entry_t e;
    int slot, room, rc;

    if (!name_ok(name)) {
        return TIKU_VFS_ENOENT;
    }
    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    if ((rc = find(m, name, &slot, &e, &room, (uint32_t *)0)) != 0) {
        return op_end(m, rc);
    }
    if (slot < 0) {
        return op_end(m, TIKU_VFS_ENOENT);
    }
    rc = slot_clear(m, (uint8_t)slot);
    if (rc == 0) {
        rc = bits_put(m, &g, e.start, blocks_for(e.len), 0u);
    }
    return op_end(m, rc);
}

int
tiku_mailbox_list(const tiku_mailbox_medium_t *m, tiku_mailbox_list_fn fn,
                  void *ctx)
{
    geo_t g;
    entry_t e;
    int s, rc;

    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    for (s = 0; s < (int)TIKU_MAILBOX_FILES; s++) {
        rc = slot_read(m, (uint8_t)s, &e);
        if (rc < 0) {
            return op_end(m, rc);
        }
        if (rc == SLOT_NEVER) {
            break;
        }
        if (rc == SLOT_VALID) {
            fn(e.name, e.len, ctx);
        }
    }
    return op_end(m, 0);
}

int
tiku_mailbox_check(const tiku_mailbox_medium_t *m, tiku_mailbox_stats_t *out)
{
    uint8_t buf[MB_CHUNK];
    geo_t g;
    entry_t e;
    int s, rc;
    uint32_t at, clear = 0UL, longest = 0UL;

    memset(out, 0, sizeof(*out));
    if ((rc = op_begin(m, &g)) != 0) {
        return rc;
    }
    for (s = 0; s < (int)TIKU_MAILBOX_FILES; s++) {
        uint32_t c = 0xFFFFFFFFUL, done;

        rc = slot_read(m, (uint8_t)s, &e);
        if (rc < 0) {
            return op_end(m, rc);
        }
        if (rc == SLOT_NEVER) {
            break;
        }
        if (rc != SLOT_VALID) {
            continue;
        }
        out->files++;
        out->used += e.len;
        if (is_kept(e.name)) {
            out->kept++;
        } else {
            uint8_t mark = 0u;

            if ((rc = mark_get(m, &g, (uint8_t)s, &mark)) != 0) {
                return op_end(m, rc);
            }
            if (mark == 1u) {
                out->read++;
            }
        }
        for (done = 0UL; done < e.len; done += MB_CHUNK) {
            uint32_t n = (e.len - done < MB_CHUNK) ? e.len - done : MB_CHUNK;

            rc = rd(m, g.data + e.start * TIKU_MAILBOX_BLOCK + done, buf, n);
            if (rc != 0) {
                return op_end(m, rc);
            }
            c = tiku_nvm_crc32_update(c, buf, (size_t)n);
        }
        if ((c ^ 0xFFFFFFFFUL) != e.dcrc) {
            out->bad++;
        }
    }
    rc = bits_rebuild(m, &g, &out->reclaimed);
    if (rc == 0) {
        rc = bits_scan(m, &g, 0UL, &at, &clear, &longest);
    }
    out->free = clear * TIKU_MAILBOX_BLOCK;
    out->largest = longest * TIKU_MAILBOX_BLOCK;
    return op_end(m, rc);
}
