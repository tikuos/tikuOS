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
 *   [ superblock | directory[nfiles] | data[nslots] ]
 *   superblock : magic (4) + geometry descriptor (7 x 4)
 *   dirent     : gate (4) + run (4) + name[NAME_MAX]    (one per file)
 *   data slot  : length (4) + content[SLOT_DATA]        (nslots = nfiles + 1)
 *
 * nfiles is DERIVED AT MOUNT from the extent the backend reports, and recorded
 * in the superblock so a store is never parsed under a geometry it was not
 * written with.  It is not a compile-time constant: the only C-side numbers are
 * TIKU_TFS_MAX_SLOTS (a ceiling that sizes the allocation bitmap) and
 * TIKU_TFS_MIN_SLOTS (a floor mount refuses to go below).
 *
 * SPANNED FILES.  A file owns a RUN of `span` CONTIGUOUS slots, and the dirent
 * packs that run into the single word it already had: first | span<<16.  Only
 * the FIRST slot's length word is metadata -- the rest of the run is content --
 * so a run's bytes are one contiguous range starting at slot_off(fs, first)+4 and
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
 * Superblock = magic + an UNPACKED geometry descriptor, one u32 per parameter,
 * compared element-wise at mount.
 *
 * Every dirent and slot offset is a function of the geometry, so a store
 * written with one geometry must never be parsed with another: the magic would
 * still match while every offset had moved, and the mount would either report
 * corruption or hand back another file's bytes.  Recording the geometry the
 * store was formatted with, and refusing to mount anything else, is what makes
 * a capacity change self-migrating -- it reads as virgin and reformats.
 *
 * WHY UNPACKED, having previously been four bit-fields in one word.  The packed
 * word was exactly full ([31:28] version, [27:16] MAX_FILES, [15:5]
 * SLOT_BYTES/4, [4:0] NAME_MAX) and it did not encode TIKU_TFS_SECT -- which
 * DOES move the data region, because TIKU_TFS_DATA_OFF is sector-aligned.  So
 * distinct layouts aliased to one word: on RP2350's geometry, six legal SECT
 * values all encode as 0x43A68018 while producing five different data-region
 * bases, and nothing at build time or mount time could tell them apart.  A
 * store formatted under one and mounted under another finds the directory where
 * it expects and the DATA somewhere else.
 *
 * That is not fixable by re-laying bits, because the word had none spare; and
 * the same crowding capped NAME_MAX at 31 and the format version at 15.  One
 * word per parameter costs 24 bytes of a multi-megabyte store, removes every
 * width limit, and makes adding a parameter later a one-line change instead of
 * another format break.  The offsets are recorded alongside the inputs so a
 * future input that feeds them cannot alias the way SECT did.
 */
#define TFS_FMT_VERSION  5u        /* 5: unpacked geometry descriptor        */
                                   /* 4: spanned files (dirent run word)     */

/* Descriptor word indices.  Word 0 is the magic and is written LAST. */
#define TFS_SB_MAGIC_W   0u
#define TFS_SB_VER_W     1u
#define TFS_SB_FILES_W   2u
#define TFS_SB_SLOT_W    3u
#define TFS_SB_NAME_W    4u
#define TFS_SB_SECT_W    5u        /* the field whose absence caused aliasing */
#define TFS_SB_DE_W      6u
#define TFS_SB_DATA_W    7u        /* derived, recorded so inputs cannot alias */
#define TFS_SB_WORDS     8u

#define TFS_GATE     0x4C495645u   /* "LIVE" -- directory entry is in use */

#define TFS_ALIGN4(n)   (((n) + 3u) & ~3u)

#define TFS_SB_BYTES    TIKU_TFS_SB_BYTES                   /* magic + geometry */
#define TFS_DE_BYTES    TFS_ALIGN4(8u + TIKU_TFS_NAME_MAX)  /* gate+slot+name  */
#define TFS_DIR_OFF     TFS_SB_BYTES
/* TFS_DIR_BYTES / TFS_DATA_OFF / TFS_REGION were compile-time; the directory
 * length and the data base now follow the DERIVED file count, so they live in
 * tiku_tfs_t (fs->nfiles, fs->data_off) and are computed at mount. */
/* Slot size and data-region base are sector-aligned in the header (TIKU_TFS_SECT)
 * so each file's data slot owns whole erase sectors; single-sourced here. */
#define TFS_SLOT_BYTES  TIKU_TFS_SLOT_BYTES
/* Smallest extent this build will mount: the floor's worth of store. */
#define TFS_MIN_REGION  TIKU_TFS_EXTENT_FOR_SLOTS(TIKU_TFS_MIN_SLOTS)

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
typedef char tfs_maxslots_check[(TIKU_TFS_MAX_SLOTS <= 0xFFFFu) ? 1 : -1];
typedef char tfs_floor_check[(TIKU_TFS_MIN_SLOTS <= TIKU_TFS_MAX_SLOTS) ? 1 : -1];

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
static size_t slot_off(tiku_tfs_t *fs, unsigned s)
{
    return (size_t)fs->data_off + (size_t)s * TFS_SLOT_BYTES;
}

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
    /* Top-down: try the highest start first, walking back one slot at a time. */
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
 * Clamps the run to the slot array FIRST, so `first + k` below cannot wrap.
 * Two callers hand this a run word read straight out of a dirent -- the
 * overwrite reclaim in tiku_tfs_commit() and tiku_tfs_delete() -- so a corrupt
 * `first` must not be able to alias a live slot belonging to another file.  See
 * run_check() for why the bound is expressed as a subtraction.
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
 * @brief Validate dirent @p i's stored run and hand back its first slot + length.
 *
 * The ONE place a run word read from NVM is checked before it is used, so every
 * consumer gets the same guarantees: the run lies inside the slot array, and the
 * content length fits the run's capacity.
 *
 * NOTE ON THE BOUND -- do not "simplify" this back to `first + span > NSLOTS`.
 * Both halves come out of a 32-bit run word into `unsigned`, which is 16-bit on
 * MSP430, so that sum WRAPS for a corrupt word whose halves total past 65535 and
 * the check passes.  What follows a passing check is an unguarded index into
 * fs->slot_used (a 3-byte array on MSP430), i.e. the corruption this function
 * exists to reject would instead be written out of bounds.  `span > NSLOTS -
 * first`, guarded by `first >= NSLOTS`, cannot overflow at any width.
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
 * This is the whole of "derived geometry": the linker carves an extent, and the
 * store takes as much of it as the layout allows instead of a hand-tuned number
 * having to be recomputed whenever a code window moves.
 *
 * The closed form charges a full sector of directory padding, so it can come out
 * one file short whenever the alignment absorbs slack -- on RP2350, where SECT
 * is 4096, that is every extent.  One correction step recovers it, and one is
 * provably enough because a file costs DE_BYTES + SLOT_BYTES, which exceeds the
 * SECT-1 the estimate gave away on every geometry this builds for (asserted).
 * The loop still guards the general case rather than relying on that.
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
    /* Climb while the next file still fits, then back off if we overshot. */
    while (n + 1u <= (unsigned)TIKU_TFS_MAX_SLOTS - 1u &&
           TIKU_TFS_EXTENT_FOR_SLOTS(n + 1u) <= ext) {
        n++;
    }
    while (n > 0u && TIKU_TFS_EXTENT_FOR_SLOTS(n) > ext) {
        n--;
    }
    return n;
}

/** @brief Adopt the geometry implied by @p ext.  Returns 0 if it is too small. */
static int tfs_derive(tiku_tfs_t *fs, size_t ext)
{
    unsigned n = tfs_fit(ext);

    if (n < (unsigned)TIKU_TFS_MIN_SLOTS) {
        return 0;               /* carve shrank below what this class promises */
    }
    fs->nfiles   = (uint16_t)n;
    fs->nslots   = (uint16_t)(n + 1u);
    fs->data_off = (uint32_t)TIKU_TFS_DATA_OFF_FOR(n);
    return 1;
}

/** @brief This store's geometry descriptor value for word @p w. */
static uint32_t tfs_sb_word(tiku_tfs_t *fs, unsigned w)
{
    switch (w) {
    case TFS_SB_VER_W:   return (uint32_t)TFS_FMT_VERSION;
    case TFS_SB_FILES_W: return (uint32_t)fs->nfiles;
    case TFS_SB_SLOT_W:  return (uint32_t)TFS_SLOT_BYTES;
    case TFS_SB_NAME_W:  return (uint32_t)TIKU_TFS_NAME_MAX;
    case TFS_SB_SECT_W:  return (uint32_t)TIKU_TFS_SECT;
    case TFS_SB_DE_W:    return (uint32_t)TFS_DE_BYTES;
    case TFS_SB_DATA_W:  return fs->data_off;
    default:             return 0u;              /* padding stays zero */
    }
}

/**
 * @brief Does the stored descriptor describe the geometry this build uses?
 *
 * Element-wise, so a difference in ANY recorded parameter is a mismatch and the
 * caller reformats.  The magic is excluded: it is the separate "formatted at
 * all" flag, and it is written last so a torn format cannot look complete.
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

/** @brief Write the descriptor, then the magic.  Returns non-zero on IO error. */
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
    if (!tfs_derive(fs, fs->be->size)) {
        return TFS_ERR_NOSPACE;
    }
    /* Reformatting under an open writer would erase the directory it is about
     * to commit into.  (mount() reaches format() with wr_open already cleared.) */
    if (fs->wr_open) {
        return TFS_ERR_BUSY;
    }
    /* Invalidate the magic FIRST.  The descriptor is several words now, so
     * writing it over an already-formatted store has a window in which the
     * magic is valid but the geometry is half old and half new -- a power cut
     * there would leave a store that mounts and reads the directory at the
     * wrong offset.  Clearing the magic makes that window read as virgin, which
     * is recoverable.  (The single-word descriptor this replaced was atomic and
     * needed no such step.) */
    if (wr32(fs, TFS_SB_MAGIC_W * 4u, 0u)) {
        return TFS_ERR_IO;
    }
    /* Free every directory entry BEFORE stamping the superblock, so a valid
     * magic always implies a clean directory (a torn format reads as virgin). */
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
    fs->be = be;
    fs->mounted = 0;
    fs->wr_open = 0;          /* a remount abandons any half-open writer */
    /* Derive BEFORE reading the superblock: every offset below, including the
     * superblock comparison's own view of the directory, depends on it. */
    if (!tfs_derive(fs, be->size)) {
        return TFS_ERR_NOSPACE;
    }
    if (rd32(fs, TFS_SB_MAGIC_W * 4u) != TFS_MAGIC || !tfs_sb_matches(fs)) {
        return tiku_tfs_format(fs);          /* virgin / different geometry */
    }
    /* Rebuild the data-slot allocation map from the live directory. Every run
     * is bounds-checked and claimed slot by slot, so an overlap between two
     * files is caught here rather than discovered as corrupted content later. */
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
    if (wr32(fs, slot_off(fs, (unsigned)s) + TFS_SL_LEN, 0u)) {       /* empty slot */
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
    /* Claim the directory entry's availability up front: discovering a full
     * directory only at commit would waste the whole stream. */
    if (tfs_find(fs, name) < 0 && free_dirent(fs) < 0) {
        return TFS_ERR_NOSPACE;
    }
    /*
     * SINGLE WRITER, ENFORCED BY REFUSAL.
     *
     * The store stopped being BASIC-private: modules, radio firmware, models
     * and BASIC's own checkpoint now share it, and a streamed write spans many
     * calls with a yield between them.  Two writers interleaving would stage
     * into each other's run or race the dirent flip, so the design of record
     * requires them to serialise (§4.7, "all writers serialize through one
     * kernel lock" -- listed as enforced-in-review, i.e. not enforced).
     *
     * This REFUSES rather than blocks, deliberately.  Scheduling here is
     * cooperative, so a blocking lock could only be released by the holder
     * running again -- which needs a scheduler this file is not allowed to know
     * about (it depends on tiku_nvm_backend.h and nothing else, which is what
     * keeps it host-testable).  A refusal needs no scheduler, cannot deadlock,
     * and turns "two tenants wrote at once" from silent corruption into a
     * distinct, loggable error at the point of the mistake.
     *
     * Readers are unaffected: they map in place and never take this.
     */
    if (fs->wr_open) {
        return TFS_ERR_BUSY;
    }

    span = run_span_for(max_len);
#if defined(PLATFORM_MSP430)
    /*
     * MSP430 stays a one-slot-per-file store, exactly as it was before spans.
     *
     * The commit point is a 32-bit run word (first | span<<16), and 32-bit
     * stores are TWO instructions on a 16-bit machine -- so the "single
     * architecture-word commit" the durability model rests on only holds here
     * while the high half never changes, i.e. while every span is 1.  Break
     * that and a torn flip no longer leaves the old file: it leaves an
     * inconsistent run, mount() rejects the whole store, and every file in
     * /data is lost rather than one.
     *
     * Today no MSP430 writer can exceed a slot (the shell's transfer buffer is
     * exactly TIKU_TFS_SLOT_DATA and BASIC uses the persist store on this
     * part), so this refusal is currently unreachable -- which is precisely why
     * it is written down.  It is the guard that keeps the next caller from
     * silently re-enabling store-wide loss, and it keeps the redesign's stated
     * non-goal ("no MSP430 change") true.  Lifting it means giving this part a
     * 16-bit-atomic commit, not a bigger buffer.
     */
    if (span > 1u) {
        return TFS_ERR_TOOBIG;
    }
#endif
    r = free_run(fs, span);
    if (r < 0) {
        return TFS_ERR_NOSPACE;
    }
    /* Reserve in the RAM map so nothing else takes the run mid-stream. This
     * is deliberately NOT durable: if power fails before commit, the next
     * mount rebuilds the map from live dirents, none of which reference the
     * staged run -- so it is free again with no cleanup pass. */
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

    if (w == NULL || !w->active) {
        return TFS_ERR_INVAL;
    }
    fs = w->fs;
    if (wr32(fs, slot_off(fs, w->first) + TFS_SL_LEN, (uint32_t)w->off)) {
        return TFS_ERR_IO;
    }
    i = tfs_find(fs, w->name);
    if (i < 0) {
        char nb[TIKU_TFS_NAME_MAX];
        i = free_dirent(fs);
        if (i < 0) {
            return TFS_ERR_NOSPACE;            /* directory filled mid-stream */
        }
        /* Create-with-content is one transaction: the run and the name are
         * already durable, so stamping GATE last is the single commit point.
         * Creating the entry earlier would expose a durable empty file if
         * power failed before the content landed. */
        memset(nb, 0, sizeof nb);
        memcpy(nb, w->name, strlen(w->name));
        if (wr(fs, dirent_off((unsigned)i) + TFS_DE_NAME,
               nb, TIKU_TFS_NAME_MAX) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
                 TFS_RUN_MAKE(w->first, w->span)) ||
            wr32(fs, dirent_off((unsigned)i) + TFS_DE_GATE, TFS_GATE)) {
            return TFS_ERR_IO;
        }
    } else {
        /* Atomic flip: one aligned word repoints the dirent at the new run.
         * A power cut before it leaves the dirent on the OLD run. */
        uint32_t old = de_run(fs, (unsigned)i);
        if (wr32(fs, dirent_off((unsigned)i) + TFS_DE_SLOT,
                 TFS_RUN_MAKE(w->first, w->span))) {
            return TFS_ERR_IO;
        }
        run_mark(fs, TFS_RUN_FIRST(old), TFS_RUN_SPAN(old), 0);  /* reclaim */
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

/*
 * Whole-buffer write, expressed as a one-chunk stream.  Sharing the streamed
 * path is the point: allocation, the commit sequence and the crash discipline
 * exist once, so the two entry points cannot drift apart.
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
    {
        unsigned f;
        uint32_t n;
        int rc = run_check(fs, (unsigned)i, &f, &n);
        if (rc != TFS_OK) {
            return rc;                 /* never hand out an unchecked length */
        }
        s = f;
        /* One pointer covers the whole run: only the first slot's length word is
         * metadata, so the content bytes are contiguous across the span. */
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
    /* Does not go through open_w, so it needs the interlock explicitly: a
     * delete during someone else's stream could reclaim slots that stream has
     * staged into. */
    if (fs->wr_open) {
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
    rc = run_check(fs, (unsigned)i, NULL, &n);  /* callers size buffers off this */
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
    for (i = 0; i < fs->nfiles; i++) {
        if (de_gate(fs, i) != TFS_GATE) {
            f++;
        }
    }
    return f;
}
