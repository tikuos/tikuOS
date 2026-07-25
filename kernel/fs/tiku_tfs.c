/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.c - Tiku File Store implementation.  See tiku_tfs.h for the model.
 *
 * On-NVM layout:
 *   [ superblock | directory[MAX_FILES] | data[NSLOTS] ]
 *   superblock : magic (4) + version-and-geometry (4)
 *   dirent     : gate (4) + run (4) + name[NAME_MAX]    (one per file)
 *   data slot  : length (4) + content[SLOT_DATA]        (NSLOTS = MAX_FILES+1)
 *
 * SPANNED FILES.  A file owns a RUN of `span` CONTIGUOUS slots, and the dirent
 * packs that run into the single word it already had: first | span<<16.  Only
 * the FIRST slot's length word is metadata -- the rest of the run is content --
 * so a run's bytes are one contiguous range starting at slot_off(first)+4 and
 * a span of n holds n*SLOT_BYTES-4 bytes.  A one-slot file is exactly the n==1
 * case of that formula, which is why single-slot behaviour is untouched and why
 * tiku_tfs_map() keeps working over a span: contiguous in NVM means one pointer.
 *
 * Overwrite stages a fresh run then flips the dirent's run word in one aligned
 * write -> a torn overwrite leaves the OLD file, as before.  What spans change
 * is the space rule: replacing an n-slot file transiently needs n MORE free
 * slots, so an overwrite can now fail with NOSPACE where the old
 * one-shadow-slot guarantee made it impossible.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_tfs.h"

#include <string.h>

/*---------------------------------------------------------------------------*/
/* ON-NVM LAYOUT                                                             */
/*---------------------------------------------------------------------------*/

#define TFS_MAGIC    0x54465331u   /* "TFS1" -- store is formatted */

/*
 * Superblock word 1 = format version PLUS the geometry that produced the
 * on-NVM layout:
 *
 *     [31:28] format version   [27:16] MAX_FILES   [15:5] SLOT_BYTES/4
 *                                                  [4:0]  NAME_MAX
 *
 * Every dirent and slot offset is a function of those three numbers, so a
 * store written with one geometry MUST NOT be parsed with another -- the magic
 * would still match while every offset had moved, and the mount would either
 * report corruption or hand back another file's bytes.  Folding the geometry
 * into the word mount() already compares for equality makes any change to the
 * capacity table (or a -DTIKU_TFS_SLOT_DATA override) self-migrating: the
 * store reads as virgin and reformats, with no extra superblock field and no
 * change to the layout itself.
 *
 * It also retires the old per-platform version ladder.  RP2350 needed v2
 * purely because its slots are sector-sized -- i.e. because its geometry
 * differed -- which is now simply what the geometry field says.
 */
#define TFS_FMT_VERSION  4u        /* 4: spanned files (dirent run word) */
#define TFS_VERSION                                                           \
    (((uint32_t)TFS_FMT_VERSION      << 28) |                                 \
     ((uint32_t)TIKU_TFS_MAX_FILES   << 16) |                                 \
     (((uint32_t)TIKU_TFS_SLOT_BYTES / 4u) << 5) |                            \
      (uint32_t)TIKU_TFS_NAME_MAX)

/* Each field must fit its bit range, or distinct geometries could encode to the
 * same word and a stale store would mount with the wrong offsets.  C89-portable
 * assertions (this file also builds under -std=c89). */
typedef char tfs_geom_files_check[(TIKU_TFS_MAX_FILES <= 4095) ? 1 : -1];
typedef char tfs_geom_slot_check[((TIKU_TFS_SLOT_BYTES / 4u) <= 2047u &&
                                  (TIKU_TFS_SLOT_BYTES % 4u) == 0u) ? 1 : -1];
typedef char tfs_geom_name_check[(TIKU_TFS_NAME_MAX <= 31) ? 1 : -1];

#define TFS_GATE     0x4C495645u   /* "LIVE" -- directory entry is in use */

#define TFS_ALIGN4(n)   (((n) + 3u) & ~3u)

#define TFS_SB_BYTES    8u                                  /* magic + version */
#define TFS_DE_BYTES    TFS_ALIGN4(8u + TIKU_TFS_NAME_MAX)  /* gate+slot+name  */
#define TFS_DIR_OFF     TFS_SB_BYTES
#define TFS_DIR_BYTES   (TFS_DE_BYTES * TIKU_TFS_MAX_FILES)
/* Slot size and data-region base are sector-aligned in the header (TIKU_TFS_SECT)
 * so each file's data slot owns whole erase sectors; single-sourced here. */
#define TFS_SLOT_BYTES  TIKU_TFS_SLOT_BYTES
#define TFS_DATA_OFF    TIKU_TFS_DATA_OFF
#define TFS_REGION      (TFS_DATA_OFF + TFS_SLOT_BYTES * TIKU_TFS_NSLOTS)

/* The public TIKU_TFS_REGION_BYTES (tiku_tfs.h) must match this internal
 * layout -- C89-portable static assertion (the lib also builds under -std=c89). */
typedef char tfs_region_size_check[(TFS_REGION == TIKU_TFS_REGION_BYTES) ? 1 : -1];

/* field offsets within a dirent / a slot */
#define TFS_DE_GATE  0u
#define TFS_DE_SLOT  4u            /* the RUN word: first | span<<16 */
#define TFS_DE_NAME  8u
#define TFS_SL_LEN   0u
#define TFS_SL_DATA  4u

/*
 * Run word packing.  Both halves must fit 16 bits, which bounds NSLOTS -- a
 * 3.5 MB store is ~900 slots, so the headroom is ample, but assert it rather
 * than assume it: an overflow here would alias two different runs onto the
 * same word and silently hand back another file's bytes.
 */
typedef char tfs_nslots_check[(TIKU_TFS_NSLOTS <= 0xFFFFu) ? 1 : -1];

#define TFS_RUN_MAKE(first, span)  ((uint32_t)(first) | ((uint32_t)(span) << 16))
#define TFS_RUN_FIRST(w)           ((unsigned)((w) & 0xFFFFu))
#define TFS_RUN_SPAN(w)            ((unsigned)((w) >> 16))

/** @brief Content capacity of a run of @p span slots (the n==1 case == SLOT_DATA). */
#define TFS_RUN_CAP(span)  ((size_t)(span) * TFS_SLOT_BYTES - TFS_SL_DATA)

/*---------------------------------------------------------------------------*/
/* LOW-LEVEL ACCESS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Alignment-safe 32-bit read from the NVM region at byte offset @p off.
 */
static uint32_t rd32(tiku_tfs_t *fs, size_t off)
{
    uint32_t v;
    memcpy(&v, fs->be->base + off, sizeof v);   /* alignment-safe read */
    return v;
}

/**
 * @brief Backend write of @p n bytes at @p off; TFS_OK or TFS_ERR_IO.
 */
static int wr(tiku_tfs_t *fs, size_t off, const void *p, size_t n)
{
    return (fs->be->write(fs->be, off, p, n) == 0) ? TFS_OK : TFS_ERR_IO;
}

/**
 * @brief Backend write of a 32-bit word @p v at offset @p off.
 */
static int wr32(tiku_tfs_t *fs, size_t off, uint32_t v)
{
    return wr(fs, off, &v, sizeof v);
}

static size_t dirent_off(unsigned i) { return TFS_DIR_OFF + (size_t)i * TFS_DE_BYTES; }
static size_t slot_off(unsigned s)   { return TFS_DATA_OFF + (size_t)s * TFS_SLOT_BYTES; }

static uint32_t de_gate(tiku_tfs_t *fs, unsigned i) { return rd32(fs, dirent_off(i) + TFS_DE_GATE); }
/** @brief Raw run word of dirent @p i (first | span<<16). */
static uint32_t de_run(tiku_tfs_t *fs, unsigned i) { return rd32(fs, dirent_off(i) + TFS_DE_SLOT); }
/** @brief First slot of dirent @p i's run. */
static unsigned de_first(tiku_tfs_t *fs, unsigned i) { return TFS_RUN_FIRST(de_run(fs, i)); }
/** @brief Slot count of dirent @p i's run. */
static unsigned de_span(tiku_tfs_t *fs, unsigned i) { return TFS_RUN_SPAN(de_run(fs, i)); }
/**
 * @brief Pointer to the NUL-padded name field of directory entry @p i.
 */
static const char *de_name(tiku_tfs_t *fs, unsigned i)
{
    return (const char *)(fs->be->base + dirent_off(i) + TFS_DE_NAME);
}
/**
 * @brief Length field of data slot @p s (out-of-range index clamps to 0).
 */
static uint32_t sl_len(tiku_tfs_t *fs, unsigned s)
{
    /* Defensive: stat/list/list_dir pass de_slot() straight in, so a corrupt
     * dirent could index past the data region -- clamp out-of-range to 0. The
     * bounds-checked callers (read/map/mount) pass an already-validated index. */
    if (s >= TIKU_TFS_NSLOTS) {
        return 0u;
    }
    return rd32(fs, slot_off(s) + TFS_SL_LEN);
}
/**
 * @brief Pointer to the content bytes of data slot @p s in the NVM region.
 */
static const uint8_t *sl_data(tiku_tfs_t *fs, unsigned s)
{
    return fs->be->base + slot_off(s) + TFS_SL_DATA;
}

/* in-RAM data-slot allocation map */
static void bm_set(uint8_t *bm, unsigned i) { bm[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static void bm_clr(uint8_t *bm, unsigned i) { bm[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }
static int  bm_get(const uint8_t *bm, unsigned i) { return (bm[i >> 3] >> (i & 7u)) & 1u; }

/**
 * @brief Look up a file by exact name in the directory.
 *
 * Scans every directory entry for a live (gated) dirent whose name matches.
 *
 * @param name  NUL-terminated file name to match.
 * @return      Directory index of the match, or -1 if not found.
 */
static int tfs_find(tiku_tfs_t *fs, const char *name)
{
    unsigned i;
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE && strcmp(de_name(fs, i), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Find the first unused directory entry.
 *
 * @return  Index of the first non-live dirent, or -1 if the directory is full.
 */
static int free_dirent(tiku_tfs_t *fs)
{
    unsigned i;
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Find @p span contiguous free data slots.
 *
 * DOUBLE-ENDED, and deliberately so.  Single-slot files -- the small, churny
 * majority -- are packed from the BOTTOM up, exactly as before spans existed.
 * Multi-slot runs are placed from the TOP down, because they are the large,
 * long-lived tenants (model weights, radio firmware, a saved program) and
 * interleaving them with churn is what fragments a store until a big
 * contiguous run can no longer be found even with plenty of free space.
 * Growing the two populations toward each other keeps the large-run end
 * unbroken while confining reuse to the end where contiguity does not matter.
 *
 * @return  Index of the run's first slot, or -1 if no such run exists.
 */
static int free_run(tiku_tfs_t *fs, unsigned span)
{
    unsigned s, k;

    if (span == 0u || span > TIKU_TFS_NSLOTS) {
        return -1;
    }
    if (span == 1u) {
        for (s = 0; s < TIKU_TFS_NSLOTS; s++) {
            if (!bm_get(fs->slot_used, s)) {
                return (int)s;
            }
        }
        return -1;
    }
    /* Top-down: try the highest start first, walking back one slot at a time. */
    for (s = TIKU_TFS_NSLOTS - span + 1u; s-- > 0u; ) {
        for (k = 0u; k < span; k++) {
            if (bm_get(fs->slot_used, s + k)) {
                break;
            }
        }
        if (k == span) {
            return (int)s;
        }
    }
    return -1;
}

/** @brief Mark every slot of a run allocated / free. */
static void run_mark(tiku_tfs_t *fs, unsigned first, unsigned span, int used)
{
    unsigned k;
    for (k = 0u; k < span; k++) {
        if (first + k < TIKU_TFS_NSLOTS) {
            if (used) {
                bm_set(fs->slot_used, first + k);
            } else {
                bm_clr(fs->slot_used, first + k);
            }
        }
    }
}

/** @brief Slots needed to hold @p len content bytes (never fewer than one). */
static unsigned run_span_for(size_t len)
{
    unsigned span = 1u;
    while (TFS_RUN_CAP(span) < len) {
        span++;
    }
    return span;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

size_t tiku_tfs_region_size(void)
{
    return TFS_REGION;
}

int tiku_tfs_format(tiku_tfs_t *fs)
{
    unsigned i;
    if (fs == NULL || fs->be == NULL || fs->be->base == NULL) {
        return TFS_ERR_INVAL;
    }
    if (fs->be->size < TFS_REGION) {
        return TFS_ERR_NOSPACE;
    }
    /* Free every directory entry BEFORE stamping the superblock, so a valid
     * magic always implies a clean directory (a torn format reads as virgin). */
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (wr32(fs, dirent_off(i) + TFS_DE_GATE, 0u)) {
            return TFS_ERR_IO;
        }
    }
    if (wr32(fs, 4, TFS_VERSION) || wr32(fs, 0, TFS_MAGIC)) {
        return TFS_ERR_IO;
    }
    memset(fs->slot_used, 0, sizeof fs->slot_used);
    fs->mounted = 1;
    return TFS_OK;
}

int tiku_tfs_mount(tiku_tfs_t *fs, tiku_nvm_backend_t *be)
{
    unsigned i;
    if (fs == NULL || be == NULL || be->base == NULL || be->write == NULL) {
        return TFS_ERR_INVAL;
    }
    fs->be = be;
    fs->mounted = 0;
    if (be->size < TFS_REGION) {
        return TFS_ERR_NOSPACE;
    }
    if (rd32(fs, 0) != TFS_MAGIC || rd32(fs, 4) != TFS_VERSION) {
        return tiku_tfs_format(fs);            /* virgin / wrong-version NVM */
    }
    /* Rebuild the data-slot allocation map from the live directory. Every run
     * is bounds-checked and claimed slot by slot, so an overlap between two
     * files is caught here rather than discovered as corrupted content later. */
    memset(fs->slot_used, 0, sizeof fs->slot_used);
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            unsigned first = de_first(fs, i);
            unsigned span  = de_span(fs, i);
            unsigned k;
            if (span == 0u || first + span > TIKU_TFS_NSLOTS ||
                sl_len(fs, first) > TFS_RUN_CAP(span)) {
                return TFS_ERR_CORRUPT;
            }
            for (k = 0u; k < span; k++) {
                if (bm_get(fs->slot_used, first + k)) {
                    return TFS_ERR_CORRUPT;     /* two names own one slot */
                }
                bm_set(fs->slot_used, first + k);
            }
        }
    }
    fs->mounted = 1;
    return TFS_OK;
}

int tiku_tfs_create(tiku_tfs_t *fs, const char *name)
{
    size_t nl;
    int i, s;
    char nb[TIKU_TFS_NAME_MAX];

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    nl = strlen(name);
    if (nl == 0 || nl >= TIKU_TFS_NAME_MAX) {
        return TFS_ERR_NAMELEN;
    }
    if (tfs_find(fs, name) >= 0) {
        return TFS_ERR_EXISTS;
    }
    i = free_dirent(fs);
    if (i < 0) {
        return TFS_ERR_NOSPACE;
    }
    s = free_run(fs, 1u);                       /* empty file: one slot */
    if (s < 0) {
        return TFS_ERR_NOSPACE;
    }
    if (wr32(fs, slot_off((unsigned)s) + TFS_SL_LEN, 0u)) {       /* empty slot */
        return TFS_ERR_IO;
    }
    memset(nb, 0, sizeof nb);
    memcpy(nb, name, nl);
    /* name + run, then GATE last (the commit point). */
    if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME, nb, TIKU_TFS_NAME_MAX) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
             TFS_RUN_MAKE((unsigned)s, 1u)) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
        return TFS_ERR_IO;
    }
    bm_set(fs->slot_used, (unsigned)s);
    return TFS_OK;
}

/**
 * @brief Write a file's content atomically, creating it if absent.
 *
 * Stages the content and its length in a fresh shadow slot, then repoints the
 * dirent at that slot with one aligned word write.  A power cut before the
 * flip leaves the old content intact; after it, the old slot is reclaimed.
 *
 * @param fs    Mounted file store.
 * @param name  File to write (created on first write).
 * @param data  Source bytes; may be NULL only when @p len is 0.
 * @param len   Byte count, at most TIKU_TFS_SLOT_DATA.
 * @return      TFS_OK, or a negative TFS_ERR_* code.
 */
int tiku_tfs_write(tiku_tfs_t *fs, const char *name, const void *data, size_t len)
{
    int      i, ns;
    unsigned span;
    uint32_t old;

    if (fs == NULL || !fs->mounted || name == NULL || (len && data == NULL)) {
        return TFS_ERR_INVAL;
    }
    if (len > TIKU_TFS_FILE_MAX) {
        return TFS_ERR_TOOBIG;
    }
    if (strlen(name) == 0 || strlen(name) >= TIKU_TFS_NAME_MAX) {
        return TFS_ERR_NAMELEN;
    }
    span = run_span_for(len);
    i = tfs_find(fs, name);
    if (i < 0) {
        char nb[TIKU_TFS_NAME_MAX];
        i = free_dirent(fs);
        if (i < 0) {
            return TFS_ERR_NOSPACE;
        }
        ns = free_run(fs, span);
        if (ns < 0) {
            return TFS_ERR_NOSPACE;
        }
        /* Create-with-content is one transaction: stage the final run and
         * directory payload, then stamp GATE last.  Calling create() first
         * would expose a durable empty file if power failed before content. */
        if ((len && wr(fs, slot_off((unsigned)ns) + TFS_SL_DATA, data, len)) ||
            wr32(fs, slot_off((unsigned)ns) + TFS_SL_LEN, (uint32_t)len)) {
            return TFS_ERR_IO;
        }
        memset(nb, 0, sizeof nb);
        memcpy(nb, name, strlen(name));
        if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME,
               nb, TIKU_TFS_NAME_MAX) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
                 TFS_RUN_MAKE((unsigned)ns, span)) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
            return TFS_ERR_IO;
        }
        run_mark(fs, (unsigned)ns, span, 1);
        return TFS_OK;
    }
    /* Replace: the new run must coexist with the old one until the flip, so a
     * big file needs its own size free ON TOP of what it already occupies. */
    ns = free_run(fs, span);
    if (ns < 0) {
        return TFS_ERR_NOSPACE;
    }
    if ((len && wr(fs, slot_off((unsigned)ns) + TFS_SL_DATA, data, len)) ||
        wr32(fs, slot_off((unsigned)ns) + TFS_SL_LEN, (uint32_t)len)) {
        return TFS_ERR_IO;
    }
    /* Atomic flip: one aligned word repoints the dirent at the new run. A
     * power cut before this leaves the dirent on the OLD run (old content). */
    old = de_run(fs, (unsigned)i);
    run_mark(fs, (unsigned)ns, span, 1);
    if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
             TFS_RUN_MAKE((unsigned)ns, span))) {
        run_mark(fs, (unsigned)ns, span, 0);   /* flip failed: shadow is free */
        return TFS_ERR_IO;
    }
    run_mark(fs, TFS_RUN_FIRST(old), TFS_RUN_SPAN(old), 0);   /* reclaim */
    return TFS_OK;
}

/**
 * @brief Copy a file's content into a caller-supplied buffer.
 *
 * Copies at most @p max bytes; the true stored length is reported via
 * @p out_len so the caller can detect truncation.
 *
 * @param fs       Mounted file store.
 * @param name     File to read.
 * @param buf      Destination buffer; may be NULL only when @p max is 0.
 * @param max      Capacity of @p buf in bytes.
 * @param out_len  If non-NULL, receives the file's full stored length.
 * @return         TFS_OK, or a negative TFS_ERR_* code.
 */
int tiku_tfs_read(tiku_tfs_t *fs, const char *name, void *buf, size_t max, size_t *out_len)
{
    int i;
    uint32_t s, len;
    size_t n;

    if (fs == NULL || !fs->mounted || name == NULL || (max && buf == NULL)) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_first(fs, (unsigned)i);
    if (s + de_span(fs, (unsigned)i) > TIKU_TFS_NSLOTS) {
        return TFS_ERR_CORRUPT;
    }
    len = sl_len(fs, (unsigned)s);
    if ((size_t)len > TFS_RUN_CAP(de_span(fs, (unsigned)i))) {
        return TFS_ERR_CORRUPT;
    }
    n = (len < max) ? len : max;
    if (n) {
        memcpy(buf, sl_data(fs, (unsigned)s), n);
    }
    if (out_len) {
        *out_len = len;
    }
    return TFS_OK;
}

/**
 * @brief Zero-copy view of a file's content within the NVM region.
 *
 * Returns a pointer directly into the backing store (no copy); it stays valid
 * until the file is overwritten or deleted.
 *
 * @param fs    Mounted file store.
 * @param name  File to map.
 * @param p     Receives a pointer to the content bytes in the region.
 * @param len   Receives the content length in bytes.
 * @return      TFS_OK, or a negative TFS_ERR_* code.
 */
int tiku_tfs_map(tiku_tfs_t *fs, const char *name, const void **p, size_t *len)
{
    int i;
    uint32_t s;

    if (fs == NULL || !fs->mounted || name == NULL || p == NULL || len == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_first(fs, (unsigned)i);
    if (s + de_span(fs, (unsigned)i) > TIKU_TFS_NSLOTS) {
        return TFS_ERR_CORRUPT;
    }
    /* One pointer covers the whole run: only the first slot's length word is
     * metadata, so the content bytes are contiguous across the span. */
    *p = sl_data(fs, (unsigned)s);             /* points into the NVM region */
    *len = sl_len(fs, (unsigned)s);
    return TFS_OK;
}

int tiku_tfs_delete(tiku_tfs_t *fs, const char *name)
{
    int i;
    uint32_t s;

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    s = de_run(fs, (unsigned)i);
    if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, 0u)) {   /* commit */
        return TFS_ERR_IO;
    }
    run_mark(fs, TFS_RUN_FIRST(s), TFS_RUN_SPAN(s), 0);
    return TFS_OK;
}

int tiku_tfs_stat(tiku_tfs_t *fs, const char *name, size_t *len)
{
    int i;
    if (fs == NULL || !fs->mounted || name == NULL || len == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    *len = sl_len(fs, de_first(fs, (unsigned)i));
    return TFS_OK;
}

int tiku_tfs_list(tiku_tfs_t *fs, tiku_tfs_iter_cb cb, void *ctx)
{
    unsigned i;
    int n = 0;
    if (fs == NULL || !fs->mounted) {
        return TFS_ERR_INVAL;
    }
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            if (cb) {
                cb(de_name(fs, i), sl_len(fs, de_first(fs, i)), ctx);
            }
            n++;
        }
    }
    return n;
}

/* List the IMMEDIATE children under @p prefix, presenting the flat store as a
 * tree (path-as-name): a file directly in the directory is reported by its leaf
 * name; a deeper path contributes its first segment ONCE, with a trailing '/'
 * so the caller can tell folders from files.  prefix is "" for the store root
 * or "logs/" for a sub-folder; the empty marker entry "<dir>/" (mkdir) is
 * skipped here but still surfaces the folder one level up. */
int tiku_tfs_list_dir(tiku_tfs_t *fs, const char *prefix,
                      tiku_tfs_iter_cb cb, void *ctx)
{
    unsigned i, j;
    size_t   plen;
    int      n = 0;

    if (fs == NULL || !fs->mounted || prefix == NULL) {
        return TFS_ERR_INVAL;
    }
    plen = strlen(prefix);

    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        const char *name, *rest, *slash;
        if (de_gate(fs, i) != TFS_GATE) {
            continue;
        }
        name = de_name(fs, i);
        if (strncmp(name, prefix, plen) != 0) {
            continue;                          /* not under this directory */
        }
        rest = name + plen;
        if (*rest == '\0') {
            continue;                          /* the directory's own marker */
        }
        slash = strchr(rest, '/');
        if (slash == NULL) {                   /* a file in this directory */
            if (cb) {
                cb(rest, sl_len(fs, de_first(fs, i)), ctx);
            }
            n++;
        } else {                               /* a sub-folder: first segment */
            size_t seglen = (size_t)(slash - rest) + 1;   /* include the '/' */
            int    dup = 0;
            for (j = 0; j < i; j++) {           /* emit once: dedup vs earlier */
                const char *nm2;
                if (de_gate(fs, j) != TFS_GATE) {
                    continue;
                }
                nm2 = de_name(fs, j);
                if (strncmp(nm2, prefix, plen) == 0 &&
                    strncmp(nm2 + plen, rest, seglen) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup && cb) {
                char fbuf[TIKU_TFS_NAME_MAX + 1];
                if (seglen < sizeof fbuf) {
                    memcpy(fbuf, rest, seglen);          /* "<segment>/" */
                    fbuf[seglen] = '\0';
                    cb(fbuf, 0, ctx);
                    n++;
                }
            }
        }
    }
    return n;
}

size_t tiku_tfs_free_files(tiku_tfs_t *fs)
{
    unsigned i;
    size_t f = 0;
    if (fs == NULL || !fs->mounted) {
        return 0;
    }
    for (i = 0; i < TIKU_TFS_MAX_FILES; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            f++;
        }
    }
    return f;
}
