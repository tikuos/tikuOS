/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.c - Tiku File Store implementation.  See tiku_tfs.h for the model.
 *
 * On-NVM layout is [superblock | directory | data slots].  The slot count is
 * derived at mount from the extent the backend reports and recorded in the
 * superblock, so a store is never parsed under another geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_tfs.h"

#include <string.h>

/* Held leases form a static list.  mount() checks it by handle address and
 * backend overlap and does not read the tiku_tfs_t it is handed, which may be
 * uninitialised. */
#if TIKU_TFS_HOLD_ENABLE
static tiku_tfs_hold_t *tfs_holds;
/** @brief Whether a lease is held on @p fs itself. */
static int tfs_handle_held(const tiku_tfs_t *fs)
{
    const tiku_tfs_hold_t *h;
    for (h = tfs_holds; h; h = h->next) if (h->fs == fs) return 1;
    return 0;
}
/** @brief Whether a lease is held on a store whose backend overlaps @p be. */
static int tfs_backing_held(const tiku_nvm_backend_t *be)
{
    const tiku_tfs_hold_t *h;
    if (!be || !be->base) return 0;
    for (h = tfs_holds; h; h = h->next) {
        const tiku_nvm_backend_t *other = h->fs->be;
        uintptr_t a = (uintptr_t)be->base, b = (uintptr_t)other->base;
        if (a <= b ? b - a < be->size : a - b < other->size) return 1;
    }
    return 0;
}
/** @brief Whether a lease on @p fs, or on an overlapping store, blocks it. */
static int tfs_held(const tiku_tfs_t *fs)
{ return tfs_handle_held(fs) || tfs_backing_held(fs->be); }
int tiku_tfs_hold(tiku_tfs_t *fs, tiku_tfs_hold_t *hold)
{
    tiku_tfs_hold_t *h;
    if (!fs || !fs->mounted || !hold) return TFS_ERR_INVAL;
    if (fs->wr_open) return TFS_ERR_BUSY;
    for (h = tfs_holds; h; h = h->next) if (h == hold) return TFS_ERR_BUSY;
    hold->fs = fs; hold->next = tfs_holds; tfs_holds = hold;
    return TFS_OK;
}
int tiku_tfs_release(tiku_tfs_hold_t *hold)
{
    tiku_tfs_hold_t **h;
    for (h = &tfs_holds; *h; h = &(*h)->next) if (*h == hold) {
        *h = hold->next; hold->fs = NULL; hold->next = NULL;
        return TFS_OK;
    }
    return TFS_ERR_INVAL;
}
#else
/* Leases compiled out: nothing is held, and tiku_tfs_hold() and
 * tiku_tfs_release() return TFS_ERR_INVAL. */
/** @brief No lease exists in this build: returns 0. */
static int tfs_held(const tiku_tfs_t *fs) { (void)fs; return 0; }
/** @brief No lease exists in this build: returns 0. */
static int tfs_handle_held(const tiku_tfs_t *fs) { (void)fs; return 0; }
/** @brief No lease exists in this build: returns 0. */
static int tfs_backing_held(const tiku_nvm_backend_t *be) { (void)be; return 0; }
int tiku_tfs_hold(tiku_tfs_t *fs, tiku_tfs_hold_t *hold)
{ (void)fs; (void)hold; return TFS_ERR_INVAL; }
int tiku_tfs_release(tiku_tfs_hold_t *hold)
{ (void)hold; return TFS_ERR_INVAL; }
#endif

/*---------------------------------------------------------------------------*/
/* ON-NVM LAYOUT                                                             */
/*---------------------------------------------------------------------------*/

#define TFS_MAGIC    0x54465331u   /* "TFS1" -- store is formatted */

/*
 * Superblock: the magic, then a geometry descriptor of one u32 per parameter,
 * compared word by word at mount.  Every dirent and slot offset follows from
 * the geometry, so mount returns TFS_ERR_GEOMETRY for a recorded geometry that
 * differs and does not reformat.  The derived data offset is recorded beside
 * its inputs, so no two layouts share a descriptor.
 */
#define TFS_FMT_VERSION  5u        /* on-NVM format version */

/* Descriptor word indices.  Word 0 is the magic and is written last. */
#define TFS_SB_MAGIC_W   0u
#define TFS_SB_VER_W     1u
#define TFS_SB_FILES_W   2u
#define TFS_SB_SLOT_W    3u
#define TFS_SB_NAME_W    4u
#define TFS_SB_SECT_W    5u        /* erase granule; moves the data base */
#define TFS_SB_DE_W      6u
#define TFS_SB_DATA_W    7u        /* derived data offset */
#define TFS_SB_WORDS     8u

#define TFS_GATE     0x4C495645u   /* "LIVE" -- directory entry is in use */

#define TFS_ALIGN4(n)   (((n) + 3u) & ~3u)

#define TFS_SB_BYTES    TIKU_TFS_SB_BYTES                   /* magic+geometry */
#define TFS_DE_BYTES    TFS_ALIGN4(8u + TIKU_TFS_NAME_MAX)  /* gate+run+name  */
#define TFS_DIR_OFF     TFS_SB_BYTES
/* The directory length and the data base follow the file count derived at
 * mount (fs->nfiles, fs->data_off). */
/* Slot size and data base are aligned to the erase granule (TIKU_TFS_SECT) in
 * tiku_tfs.h, so each slot owns whole erase sectors. */
#define TFS_SLOT_BYTES  TIKU_TFS_SLOT_BYTES
/* Smallest extent this build will mount: the floor's worth of store. */
#define TFS_MIN_REGION  TIKU_TFS_EXTENT_FOR_SLOTS(TIKU_TFS_MIN_SLOTS)

/* field offsets within a dirent / a slot */
#define TFS_DE_GATE  0u
#define TFS_DE_SLOT  4u            /* the run word: first | span<<16 */
#define TFS_DE_NAME  8u
#define TFS_SL_LEN   0u
#define TFS_SL_DATA  4u

/*
 * Run word: first slot in the low 16 bits, span in the high 16.  The first
 * typedef fails the build if TIKU_TFS_MAX_SLOTS outgrows a half, which would
 * alias two runs onto one word; the second if the floor reaches the ceiling,
 * since a store holds at most TIKU_TFS_MAX_SLOTS - 1 files (tfs_fit()).
 */
typedef char tfs_maxslots_check[(TIKU_TFS_MAX_SLOTS <= 0xFFFFu) ? 1 : -1];
typedef char tfs_floor_check[(TIKU_TFS_MIN_SLOTS < TIKU_TFS_MAX_SLOTS) ? 1 : -1];

#define TFS_RUN_MAKE(first, span)  ((uint32_t)(first) | ((uint32_t)(span) << 16))
#define TFS_RUN_FIRST(w)           ((unsigned)((w) & 0xFFFFu))
#define TFS_RUN_SPAN(w)            ((unsigned)((w) >> 16))

/** @brief Content bytes a run of @p span slots holds (span 1: SLOT_DATA). */
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
    memcpy(&v, fs->be->base + off, sizeof v);
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

/** @brief Byte offset of directory entry @p i. */
static size_t dirent_off(unsigned i) { return TFS_DIR_OFF + (size_t)i * TFS_DE_BYTES; }
/** @brief Byte offset of data slot @p s. */
static size_t slot_off(tiku_tfs_t *fs, unsigned s)
{
    return (size_t)fs->data_off + (size_t)s * TFS_SLOT_BYTES;
}

/** @brief Gate word of dirent @p i; TFS_GATE when the entry is live. */
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
    /* list and list_dir pass de_first() unchecked, so a corrupt dirent could
     * index past the data region: an out-of-range slot reads as length 0.
     * run_check() validates the index for every other caller. */
    if (s >= fs->nslots) {
        return 0u;
    }
    return rd32(fs, slot_off(fs, s) + TFS_SL_LEN);
}
/**
 * @brief Pointer to the content bytes of data slot @p s in the NVM region.
 */
static const uint8_t *sl_data(tiku_tfs_t *fs, unsigned s)
{
    return fs->be->base + slot_off(fs, s) + TFS_SL_DATA;
}

/** @brief Mark slot @p i used in the in-RAM allocation map @p bm. */
static void bm_set(uint8_t *bm, unsigned i) { bm[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
/** @brief Mark slot @p i free in the allocation map @p bm. */
static void bm_clr(uint8_t *bm, unsigned i) { bm[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }
/** @brief Whether slot @p i is marked used in the allocation map @p bm. */
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
    for (i = 0; i < fs->nfiles; i++) {
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
    for (i = 0; i < fs->nfiles; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Find @p span contiguous free data slots.
 *
 * Double-ended: a one-slot run is taken from the bottom up, a longer run from
 * the top down, so one-slot files do not fragment the end where long runs
 * are placed.
 *
 * @return  Index of the run's first slot, or -1 if no such run exists.
 */
static int free_run(tiku_tfs_t *fs, unsigned span)
{
    unsigned s, k;

    if (span == 0u || span > fs->nslots) {
        return -1;
    }
    if (span == 1u) {
        for (s = 0; s < fs->nslots; s++) {
            if (!bm_get(fs->slot_used, s)) {
                return (int)s;
            }
        }
        return -1;
    }
    /* Top-down: the highest start first, then back one slot at a time. */
    for (s = fs->nslots - span + 1u; s-- > 0u; ) {
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

/**
 * @brief Mark every slot of a run allocated / free.
 *
 * Clamps the run to the slot array first, so `first + k` cannot wrap.  Some
 * callers pass a run word read from a dirent without run_check(); a corrupt
 * `first` marks nothing, and a corrupt span stops at the last slot.
 */
static void run_mark(tiku_tfs_t *fs, unsigned first, unsigned span, int used)
{
    unsigned k;

    if (first >= fs->nslots) {
        return;                                /* corrupt run word: ignore */
    }
    if (span > fs->nslots - first) {
        span = fs->nslots - first;
    }
    for (k = 0u; k < span; k++) {
        if (used) {
            bm_set(fs->slot_used, first + k);
        } else {
            bm_clr(fs->slot_used, first + k);
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

/**
 * @brief Validate dirent @p i's run word; return its first slot and length.
 *
 * Read, map, stat and mount validate through this.  The bound is written as
 * `span > nslots - first`: `first + span` can wrap in a 16-bit unsigned
 * (MSP430) for a corrupt word and index fs->slot_used out of bounds.
 *
 * @param i      Directory index (caller has already confirmed the gate).
 * @param first  Out: index of the run's first slot.  May be NULL.
 * @param len    Out: content length from the first slot's length word.  May be
 *               NULL.
 * @return TFS_OK, or TFS_ERR_CORRUPT if the run or the length is inconsistent.
 */
static int run_check(tiku_tfs_t *fs, unsigned i, unsigned *first, uint32_t *len)
{
    unsigned f  = de_first(fs, i);
    unsigned sp = de_span(fs, i);
    uint32_t n;

    if (sp == 0u || f >= fs->nslots || sp > fs->nslots - f) {
        return TFS_ERR_CORRUPT;
    }
    n = sl_len(fs, f);
    if ((size_t)n > TFS_RUN_CAP(sp)) {
        return TFS_ERR_CORRUPT;
    }
    if (first != NULL) {
        *first = f;
    }
    if (len != NULL) {
        *len = n;
    }
    return TFS_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

size_t tiku_tfs_region_size(void)
{
    return TFS_MIN_REGION;
}

/**
 * @brief Largest file count whose store fits in an extent of @p ext bytes.
 *
 * The closed form charges a full sector of directory padding and can come out
 * one file short; the loops then raise n while the next file fits and lower
 * it while n does not.
 *
 * @param ext Extent in bytes.
 * @return File count, clamped to the addressing ceiling; 0 if nothing fits.
 */
static unsigned tfs_fit(size_t ext)
{
    unsigned n;

    if (ext <= (size_t)TFS_SB_BYTES + TFS_SLOT_BYTES) {
        return 0u;
    }
    n = (unsigned)((ext - TFS_SB_BYTES - TFS_SLOT_BYTES - (TIKU_TFS_SECT - 1u)) /
                   (TFS_DE_BYTES + TFS_SLOT_BYTES));
    if (n > (unsigned)TIKU_TFS_MAX_SLOTS - 1u) {
        n = (unsigned)TIKU_TFS_MAX_SLOTS - 1u;      /* ceiling: bitmap bound */
    }
    /* Climb while the next file still fits, then back off on overshoot. */
    while (n + 1u <= (unsigned)TIKU_TFS_MAX_SLOTS - 1u &&
           TIKU_TFS_EXTENT_FOR_SLOTS(n + 1u) <= ext) {
        n++;
    }
    while (n > 0u && TIKU_TFS_EXTENT_FOR_SLOTS(n) > ext) {
        n--;
    }
    return n;
}

/** @brief The two geometry values that depend on the extent. */
typedef struct {
    uint16_t nfiles;
    uint32_t data_off;
} tfs_geom_t;

/** @brief Geometry for an extent of @p ext bytes.  Returns 0 if too small. */
static int tfs_geom_for(size_t ext, tfs_geom_t *g)
{
    unsigned n = tfs_fit(ext);

    if (n < (unsigned)TIKU_TFS_MIN_SLOTS) {
        return 0;               /* carve below what this class promises */
    }
    g->nfiles   = (uint16_t)n;
    g->data_off = (uint32_t)TIKU_TFS_DATA_OFF_FOR(n);
    return 1;
}

/** @brief Adopt the geometry for @p ext; 0 if the extent is too small. */
static int tfs_derive(tiku_tfs_t *fs, size_t ext)
{
    tfs_geom_t g;

    if (!tfs_geom_for(ext, &g)) {
        return 0;
    }
    fs->nfiles   = g.nfiles;
    fs->nslots   = (uint16_t)(g.nfiles + 1u);
    fs->data_off = g.data_off;
    return 1;
}

/** @brief Descriptor value for word @p w under geometry @p g. */
static uint32_t tfs_word_for(const tfs_geom_t *g, unsigned w)
{
    switch (w) {
    case TFS_SB_VER_W:   return (uint32_t)TFS_FMT_VERSION;
    case TFS_SB_FILES_W: return (uint32_t)g->nfiles;
    case TFS_SB_SLOT_W:  return (uint32_t)TFS_SLOT_BYTES;
    case TFS_SB_NAME_W:  return (uint32_t)TIKU_TFS_NAME_MAX;
    case TFS_SB_SECT_W:  return (uint32_t)TIKU_TFS_SECT;
    case TFS_SB_DE_W:    return (uint32_t)TFS_DE_BYTES;
    case TFS_SB_DATA_W:  return g->data_off;
    default:             return 0u;              /* word 0, the magic */
    }
}

/** @brief This store's geometry descriptor value for word @p w. */
static uint32_t tfs_sb_word(tiku_tfs_t *fs, unsigned w)
{
    tfs_geom_t g;

    g.nfiles   = fs->nfiles;
    g.data_off = fs->data_off;
    return tfs_word_for(&g, w);
}

/**
 * @brief Whether the stored descriptor matches the geometry this build derives.
 *
 * Compared word by word; mount returns TFS_ERR_GEOMETRY on any difference.
 * The magic is excluded: it is the separate "formatted" flag, written last so
 * a torn format does not look complete.
 *
 * @return 1 on a match, 0 otherwise.
 */
static int tfs_sb_matches(tiku_tfs_t *fs)
{
    unsigned w;
    for (w = 1u; w < TFS_SB_WORDS; w++) {
        if (rd32(fs, w * 4u) != tfs_sb_word(fs, w)) {
            return 0;
        }
    }
    return 1;
}

/** @brief Write the descriptor, then the magic; non-zero on an IO error. */
static int tfs_sb_write(tiku_tfs_t *fs)
{
    unsigned w;
    for (w = 1u; w < TFS_SB_WORDS; w++) {
        if (wr32(fs, w * 4u, tfs_sb_word(fs, w))) {
            return 1;
        }
    }
    /* Magic last: it is the commit point for the whole descriptor. */
    return wr32(fs, TFS_SB_MAGIC_W * 4u, TFS_MAGIC) ? 1 : 0;
}

int tiku_tfs_format(tiku_tfs_t *fs)
{
    unsigned i;
    if (fs == NULL || fs->be == NULL || fs->be->base == NULL) {
        return TFS_ERR_INVAL;
    }
    if (tfs_held(fs)) return TFS_ERR_BUSY;
    if (!tfs_derive(fs, fs->be->size)) {
        return TFS_ERR_NOSPACE;
    }
    /* An open streamed write commits into this directory, so format returns
     * TFS_ERR_BUSY while one is open. */
    if (fs->wr_open || tfs_held(fs)) {
        return TFS_ERR_BUSY;
    }
    /* The magic is cleared first.  The descriptor spans several words, and a
     * power cut while it is rewritten leaves it half old and half new; with
     * the magic cleared, such a store reads as no store and does not mount at
     * the wrong offsets. */
    if (wr32(fs, TFS_SB_MAGIC_W * 4u, 0u)) {
        return TFS_ERR_IO;
    }
    /* Free every directory entry before stamping the superblock, so a valid
     * magic always implies a clean directory (a torn format reads as no
     * store). */
    for (i = 0; i < fs->nfiles; i++) {
        if (wr32(fs, dirent_off(i) + TFS_DE_GATE, 0u)) {
            return TFS_ERR_IO;
        }
    }
    if (tfs_sb_write(fs)) {
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
    if (tfs_handle_held(fs) || tfs_backing_held(be)) return TFS_ERR_BUSY;
    fs->be = be;
    fs->mounted = 0;
    fs->wr_open = 0;          /* a remount abandons any half-open writer */
    /* Derive before reading the superblock: every offset below, including the
     * superblock comparison's own view of the directory, depends on it. */
    if (!tfs_derive(fs, be->size)) {
        return TFS_ERR_NOSPACE;
    }
    if (rd32(fs, TFS_SB_MAGIC_W * 4u) != TFS_MAGIC) {
        return TFS_ERR_NOSTORE;
    }
    if (!tfs_sb_matches(fs)) {
        return TFS_ERR_GEOMETRY;
    }
    /* Rebuild the data-slot allocation map from the live directory.  Every
     * run is bounds-checked and claimed slot by slot; two files that claim one
     * slot fail the mount with TFS_ERR_CORRUPT. */
    memset(fs->slot_used, 0, sizeof fs->slot_used);
    for (i = 0; i < fs->nfiles; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            unsigned first;
            unsigned span;
            unsigned k;
            if (run_check(fs, i, &first, NULL) != TFS_OK) {
                return TFS_ERR_CORRUPT;
            }
            span = de_span(fs, i);
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

int tiku_tfs_init(tiku_tfs_t *fs, tiku_nvm_backend_t *be)
{
    if (fs == NULL || be == NULL || be->base == NULL || be->write == NULL) {
        return TFS_ERR_INVAL;
    }
    if (tfs_handle_held(fs) || tfs_backing_held(be)) return TFS_ERR_BUSY;
    fs->be = be;
    fs->mounted = 0;
    fs->wr_open = 0;
    return tiku_tfs_format(fs);
}

/** @brief Alignment-safe 32-bit read at @p off from an unmounted extent. */
static uint32_t rd32_at(const uint8_t *base, size_t off)
{
    uint32_t v;
    memcpy(&v, base + off, sizeof v);
    return v;
}

/**
 * @brief Classify the store at @p base; @p full also counts live entries.
 *
 * Every count read from the medium is clamped to what @p size can hold
 * before it indexes anything.
 */
static void tfs_classify(const uint8_t *base, size_t size, int full,
                         tiku_tfs_probe_t *out)
{
    tfs_geom_t g;
    uint32_t   w[TFS_SB_WORDS];
    unsigned   i;
    int        match = 1;
    size_t     scan = 0u;
    size_t     fits;
    size_t     k;

    memset(out, 0, sizeof *out);
    if (!tfs_geom_for(size, &g)) {
        out->kind = TFS_PROBE_TOOSMALL;
        return;
    }
    for (i = 0u; i < TFS_SB_WORDS; i++) {
        w[i] = rd32_at(base, (size_t)i * 4u);
        if (i != TFS_SB_MAGIC_W && w[i] != tfs_word_for(&g, i)) {
            match = 0;
        }
    }
    fits = (size - TFS_SB_BYTES) / TFS_DE_BYTES;
    if (w[TFS_SB_MAGIC_W] == TFS_MAGIC) {
        out->version = w[TFS_SB_VER_W];
        out->nfiles  = w[TFS_SB_FILES_W];
        out->kind = (w[TFS_SB_VER_W] != TFS_FMT_VERSION) ? TFS_PROBE_VERSION
                  : match ? TFS_PROBE_COMPATIBLE : TFS_PROBE_GEOMETRY;
        scan = ((size_t)w[TFS_SB_FILES_W] < fits) ? (size_t)w[TFS_SB_FILES_W]
                                                   : fits;
    } else {
        out->nfiles = g.nfiles;
        out->kind   = match ? TFS_PROBE_TORN : TFS_PROBE_BLANK;
        scan = g.nfiles;
    }
    if (!full) {
        return;
    }
    for (k = 0u; k < scan && out->live < 0xFFFFu; k++) {
        size_t de = TFS_DIR_OFF + k * TFS_DE_BYTES;

        if (rd32_at(base, de + TFS_DE_GATE) != TFS_GATE) {
            continue;
        }
        out->live++;
        if (out->kind == TFS_PROBE_COMPATIBLE) {
            uint32_t run = rd32_at(base, de + TFS_DE_SLOT);
            unsigned f = TFS_RUN_FIRST(run), sp = TFS_RUN_SPAN(run);
            uint32_t n;

            if (sp == 0u || f > g.nfiles || sp > g.nfiles + 1u - f) {
                continue;                /* mount returns TFS_ERR_CORRUPT */
            }
            n = rd32_at(base, g.data_off + (size_t)f * TFS_SLOT_BYTES + TFS_SL_LEN);
            if ((size_t)n <= TFS_RUN_CAP(sp)) {
                out->bytes += n;
            }
        }
    }
    if (out->kind == TFS_PROBE_BLANK && out->live != 0u) {
        out->kind = TFS_PROBE_TORN;          /* entries outlived their header */
    }
    if (out->kind == TFS_PROBE_BLANK) {
        /* Blank needs every byte equal to base[0], and base[0] 0x00 or 0xFF;
         * any other content is TFS_PROBE_UNKNOWN. */
        uint8_t fill = base[0];
        if (fill != 0u && fill != 0xFFu) {
            out->kind = TFS_PROBE_UNKNOWN;
        } else {
            for (k = 1u; k < size; k++) {
                if (base[k] != fill) {
                    out->kind = TFS_PROBE_UNKNOWN;
                    break;
                }
            }
        }
    }
}

int tiku_tfs_probe(const tiku_nvm_backend_t *be, tiku_tfs_probe_t *out)
{
    if (be == NULL || be->base == NULL || out == NULL) {
        return TFS_ERR_INVAL;
    }
    tfs_classify(be->base, be->size, 1, out);
    return TFS_OK;
}

int tiku_tfs_locate(const tiku_nvm_backend_t *region, size_t step,
                    tiku_tfs_cand_t *out, int max)
{
    size_t off = 0u;
    int    n = 0;

    if (region == NULL || region->base == NULL || step == 0u) {
        return TFS_ERR_INVAL;
    }
    while (off < region->size && region->size - off >= TFS_MIN_REGION) {
        tiku_tfs_probe_t p;

        tfs_classify(region->base + off, region->size - off, 0, &p);
        if (p.kind != TFS_PROBE_BLANK && p.kind != TFS_PROBE_TOOSMALL) {
            if (out != NULL && n < max) {
                out[n].off  = (uint32_t)off;
                out[n].kind = p.kind;
            }
            n++;
        }
        if (step > region->size - off) {
            break;
        }
        off += step;
    }
    return n;
}

int tiku_tfs_may_provision(const tiku_nvm_backend_t *region, size_t base_off,
                           size_t step)
{
    tiku_nvm_backend_t at;
    tiku_tfs_probe_t   p;

    if (region == NULL || region->base == NULL || base_off >= region->size) {
        return 0;
    }
    at = *region;
    at.base = region->base + base_off;
    at.size = region->size - base_off;
    if (tiku_tfs_probe(&at, &p) != TFS_OK || p.kind != TFS_PROBE_BLANK) {
        return 0;
    }
    /* The bytes before base_off must hold the same fill, so a 1 means the
     * whole backing region is blank.  A 1 grants no ownership: region-backed
     * /data records ownership separately. */
    (void)step;
    if (base_off != 0u) {
        size_t i;
        for (i = 0u; i < base_off; i++) {
            if (region->base[i] != at.base[0]) {
                return 0;
            }
        }
    }
    return 1;
}

const char *tiku_tfs_probe_name(tfs_probe_kind_t kind)
{
    switch (kind) {
    case TFS_PROBE_COMPATIBLE: return "compatible";
    case TFS_PROBE_BLANK:      return "blank";
    case TFS_PROBE_TORN:       return "torn";
    case TFS_PROBE_GEOMETRY:   return "geometry";
    case TFS_PROBE_VERSION:    return "version";
    case TFS_PROBE_TOOSMALL:   return "too small";
    case TFS_PROBE_UNKNOWN:    return "unknown nonblank data";
    }
    return "unknown";
}

int tiku_tfs_create(tiku_tfs_t *fs, const char *name)
{
    size_t nl;
    int i, s;
    char nb[TIKU_TFS_NAME_MAX];

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    /* An open stream's commit needs a free directory entry, so create returns
     * TFS_ERR_BUSY while one is open. */
    if (fs->wr_open || tfs_held(fs)) return TFS_ERR_BUSY;
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
    if (wr32(fs, slot_off(fs, (unsigned)s) + TFS_SL_LEN, 0u)) {
        return TFS_ERR_IO;
    }
    memset(nb, 0, sizeof nb);
    memcpy(nb, name, nl);
    /* name + run, then the gate last (the commit point). */
    if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME, nb, TIKU_TFS_NAME_MAX) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
             TFS_RUN_MAKE((unsigned)s, 1u)) ||
        wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
        return TFS_ERR_IO;
    }
    bm_set(fs->slot_used, (unsigned)s);
    return TFS_OK;
}

int tiku_tfs_open_w(tiku_tfs_t *fs, tiku_tfs_wr_t *w,
                    const char *name, size_t max_len)
{
    unsigned span;
    size_t   nl;
    int      r;

    if (fs == NULL || !fs->mounted || w == NULL || name == NULL) {
        return TFS_ERR_INVAL;
    }
    if (max_len > TIKU_TFS_FILE_MAX) {
        return TFS_ERR_TOOBIG;
    }
    nl = strlen(name);
    if (nl == 0 || nl >= TIKU_TFS_NAME_MAX) {
        return TFS_ERR_NAMELEN;
    }
    /* A new name needs a free directory entry: a full directory fails here
     * with TFS_ERR_NOSPACE, and commit checks again. */
    if (tfs_find(fs, name) < 0 && free_dirent(fs) < 0) {
        return TFS_ERR_NOSPACE;
    }
    /*
     * One writer at a time.  A streamed write spans many calls with yields
     * between them, and two interleaved writers would stage into each other's
     * run or race the dirent flip, so every other writer gets TFS_ERR_BUSY at
     * once; the check never blocks.  read, map and stat ignore the interlock;
     * tiku_tfs_hold() returns TFS_ERR_BUSY until the stream commits or aborts.
     */
    if (fs->wr_open || tfs_held(fs)) {
        return TFS_ERR_BUSY;
    }

    span = run_span_for(max_len);
#if defined(PLATFORM_MSP430)
    /*
     * MSP430 keeps one slot per file.  The commit point is the 32-bit run
     * word (first | span<<16), and a 32-bit store is two instructions on this
     * 16-bit CPU, so the commit is atomic only while the high half never
     * changes, that is while every span is 1.  A torn flip of a longer span
     * leaves an inconsistent run, and mount rejects the whole store for it.
     */
    if (span > 1u) {
        return TFS_ERR_TOOBIG;
    }
#endif
    r = free_run(fs, span);
    if (r < 0) {
        return TFS_ERR_NOSPACE;
    }
    /* Reserve in the RAM map so nothing else takes the run mid-stream.  The
     * reservation is RAM-only: after a power cut before commit, the next mount
     * rebuilds the map from live dirents, none of which reference the staged
     * run, so the run is free again. */
    run_mark(fs, (unsigned)r, span, 1);
    w->fs     = fs;
    w->first  = (unsigned)r;
    w->span   = span;
    w->cap    = TFS_RUN_CAP(span);
    w->off    = 0u;
    w->active = 1;
    fs->wr_open = 1;
    memset(w->name, 0, sizeof w->name);
    memcpy(w->name, name, nl);
    return TFS_OK;
}

int tiku_tfs_write_chunk(tiku_tfs_wr_t *w, const void *data, size_t len)
{
    if (w == NULL || !w->active || (len && data == NULL)) {
        return TFS_ERR_INVAL;
    }
    if (tfs_held(w->fs)) return TFS_ERR_BUSY;
    if (len > w->cap - w->off) {
        return TFS_ERR_TOOBIG;
    }
    if (len && wr(w->fs, slot_off(w->fs, w->first) + TFS_SL_DATA + w->off, data, len)) {
        return TFS_ERR_IO;
    }
    w->off += len;
    return TFS_OK;
}

int tiku_tfs_commit(tiku_tfs_wr_t *w)
{
    tiku_tfs_t *fs;
    int         i;
    unsigned    keep;

    if (w == NULL || !w->active) {
        return TFS_ERR_INVAL;
    }
    fs = w->fs;
    if (tfs_held(fs)) return TFS_ERR_BUSY;
    if (wr32(fs, slot_off(fs, w->first) + TFS_SL_LEN, (uint32_t)w->off)) {
        return TFS_ERR_IO;
    }
    keep = run_span_for(w->off);        /* the slots the content reached */
    i = tfs_find(fs, w->name);
    if (i < 0) {
        char nb[TIKU_TFS_NAME_MAX];
        i = free_dirent(fs);
        if (i < 0) {
            return TFS_ERR_NOSPACE;            /* directory filled mid-stream */
        }
        /* Create-with-content is one transaction: the content and its length
         * word are durable, the name and run are written next, and the gate,
         * written last, is the single commit point.  A power cut before the
         * gate leaves no file. */
        memset(nb, 0, sizeof nb);
        memcpy(nb, w->name, strlen(w->name));
        if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME,
               nb, TIKU_TFS_NAME_MAX) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
                 TFS_RUN_MAKE(w->first, keep)) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
            return TFS_ERR_IO;
        }
    } else {
        /* Atomic flip: one aligned word repoints the dirent at the new run.
         * A power cut before it leaves the dirent on the old run. */
        uint32_t old = de_run(fs, (unsigned)i);
        if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
                 TFS_RUN_MAKE(w->first, keep))) {
            return TFS_ERR_IO;
        }
        run_mark(fs, TFS_RUN_FIRST(old), TFS_RUN_SPAN(old), 0);  /* reclaim */
    }
    /* The reservation's unused tail is freed after the directory names the
     * short run.  The content starts at the run's first slot, so the tail is
     * its high end; the map is RAM only, and a remount derives the same map
     * from the dirent. */
    if (keep < w->span) {
        run_mark(fs, w->first + keep, w->span - keep, 0);
    }
    w->active = 0;
    fs->wr_open = 0;
    return TFS_OK;
}

void tiku_tfs_abort(tiku_tfs_wr_t *w)
{
    if (w != NULL && w->active) {
        run_mark(w->fs, w->first, w->span, 0);
        w->active = 0;
        w->fs->wr_open = 0;          /* release the interlock */
    }
}

/**
 * @brief Write a file's content atomically, creating it if absent.
 *
 * A one-chunk stream.  A power cut before the commit word lands (the run
 * word of an existing file, the gate of a new one) leaves the old content;
 * after it, the old run is reclaimed.
 *
 * @param fs    Mounted file store.
 * @param name  File to write (created on first write).
 * @param data  Source bytes; may be NULL only when @p len is 0.
 * @param len   Byte count, at most TIKU_TFS_FILE_MAX (one slot on MSP430).
 * @return      TFS_OK, or a negative TFS_ERR_* code.
 */
int tiku_tfs_write(tiku_tfs_t *fs, const char *name, const void *data, size_t len)
{
    tiku_tfs_wr_t w;
    int           rc;

    if (len != 0u && data == NULL) {
        return TFS_ERR_INVAL;
    }
    rc = tiku_tfs_open_w(fs, &w, name, len);
    if (rc != TFS_OK) {
        return rc;
    }
    rc = tiku_tfs_write_chunk(&w, data, len);
    if (rc == TFS_OK) {
        rc = tiku_tfs_commit(&w);
    }
    if (rc != TFS_OK) {
        tiku_tfs_abort(&w);
    }
    return rc;
}

/**
 * @brief Copy a file's content into a caller-supplied buffer.
 *
 * Copies at most @p max bytes.  *out_len gets the stored length, which is
 * larger than @p max when the copy was truncated.
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
    {
        unsigned f;
        int rc = run_check(fs, (unsigned)i, &f, &len);
        if (rc != TFS_OK) {
            return rc;
        }
        s = f;
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
 * Returns a pointer directly into the backing store (no copy).
 *
 * @note The pointer is valid until the file is overwritten or deleted.
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
    {
        unsigned f;
        uint32_t n;
        int rc = run_check(fs, (unsigned)i, &f, &n);
        if (rc != TFS_OK) {
            return rc;                 /* a corrupt run sets neither output */
        }
        s = f;
        /* One pointer covers the whole run: only the first slot's length
         * word is metadata, so the content is contiguous across the span. */
        *p = sl_data(fs, (unsigned)s);         /* points into the NVM region */
        *len = n;
    }
    return TFS_OK;
}

int tiku_tfs_delete(tiku_tfs_t *fs, const char *name)
{
    int i;
    uint32_t s;

    if (fs == NULL || !fs->mounted || name == NULL) {
        return TFS_ERR_INVAL;
    }
    /* Like every other mutation, delete returns TFS_ERR_BUSY while a stream
     * or a lease is open. */
    if (fs->wr_open || tfs_held(fs)) {
        return TFS_ERR_BUSY;
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
    int i, rc;
    uint32_t n;

    if (fs == NULL || !fs->mounted || name == NULL || len == NULL) {
        return TFS_ERR_INVAL;
    }
    i = tfs_find(fs, name);
    if (i < 0) {
        return TFS_ERR_NOTFOUND;
    }
    rc = run_check(fs, (unsigned)i, NULL, &n);  /* callers size buffers by it */
    if (rc != TFS_OK) {
        return rc;
    }
    *len = n;
    return TFS_OK;
}

int tiku_tfs_list(tiku_tfs_t *fs, tiku_tfs_iter_cb cb, void *ctx)
{
    unsigned i;
    int n = 0;
    if (fs == NULL || !fs->mounted) {
        return TFS_ERR_INVAL;
    }
    for (i = 0; i < fs->nfiles; i++) {
        if (de_gate(fs, i) == TFS_GATE) {
            if (cb) {
                cb(de_name(fs, i), sl_len(fs, de_first(fs, i)), ctx);
            }
            n++;
        }
    }
    return n;
}

/* List the immediate children under @p prefix, presenting the flat store as a
 * tree (path-as-name): a file directly in the directory is reported by its leaf
 * name; a deeper path contributes its first segment once, with a trailing '/'.
 * prefix is "" for the store root or "logs/" for a sub-folder.  The empty
 * marker entry "<dir>/" (mkdir) is skipped here and reports the folder one
 * level up. */
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

    for (i = 0; i < fs->nfiles; i++) {
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
            for (j = 0; j < i; j++) {           /* emit each folder once */
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
            if (!dup) {
                char fbuf[TIKU_TFS_NAME_MAX + 1];
                if (seglen < sizeof fbuf) {
                    memcpy(fbuf, rest, seglen);          /* "<segment>/" */
                    fbuf[seglen] = '\0';
                    if (cb) cb(fbuf, 0, ctx);
                    n++;
                }
            }
        }
    }
    return n;
}

size_t tiku_tfs_used_slots(tiku_tfs_t *fs)
{
    unsigned s;
    size_t n = 0;
    if (fs == NULL || !fs->mounted) {
        return 0;
    }
    for (s = 0; s < fs->nslots; s++) {
        if (bm_get(fs->slot_used, s)) {
            n++;
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
    for (i = 0; i < fs->nfiles; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            f++;
        }
    }
    return f;
}
