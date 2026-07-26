/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.h - Tiku File Store: a tiny, bounded, power-cut-safe file store
 *              over an NVM region (the tiku_nvm_backend "B" substrate).
 *
 * Flat namespace, whole-file I/O, fixed slots -- no subdirectories, no partial
 * seek.  Designed for the actual need: named custom files (BASIC programs,
 * configs, small data blobs) kept in FRAM / MRAM / Flash across power loss.
 *
 * Durability model (per-file atomic, power-cut safe):
 *   - the store is gated by a superblock magic; an absent/invalid one formats;
 *   - each directory entry carries a magic GATE (live vs free);
 *   - content + its length live together in a physical data SLOT, and the
 *     directory entry holds the slot index;
 *   - CREATE commits by stamping the entry gate LAST;
 *   - OVERWRITE writes a fresh shadow slot, then flips the entry's slot index
 *     in one aligned word -- so a power cut leaves the OLD file, never a torn
 *     one;
 *   - DELETE commits by clearing the gate.
 * Each commit is a single 32-bit write.  On ARM that is one aligned store, the
 * same atomicity unit the persist cells rely on.  On MSP430 a 32-bit store is
 * two instructions, so the guarantee holds there only because every file is
 * one slot and the run word's span half never changes -- enforced in
 * tiku_tfs_open_w().  What the backend turns that word into varies by
 * technology, and only FRAM/RRAM/MRAM make it a true single program: on
 * erase-sector flash (RP2350) a dirent write is a read-modify-erase-program of
 * the sector holding it, which is a pre-existing property of that part, not of
 * this format -- see arch/arm-rp2350/tiku_nvm_region_rp2350.c.
 *
 * The store depends ONLY on tiku_nvm_backend.h -- it has no kernel, VFS, tier
 * or shell dependency, so it is portable and host-unit-testable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TFS_H_
#define TIKU_TFS_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_nvm_backend.h"

/*---------------------------------------------------------------------------*/
/* COMPILE-TIME LIMITS (board-aware defaults land in the caller's build)     */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_TFS_NAME_MAX
#define TIKU_TFS_NAME_MAX   24      /**< max filename length incl. NUL */
#endif
#ifndef TIKU_TFS_MAX_SLOTS
/*
 * CEILING -- the largest store this build can ADDRESS, not the store it has.
 *
 * The store's capacity is DERIVED AT MOUNT from the extent the linker actually
 * carved (be->size), so there is no per-platform capacity number here any more.
 * What remains is a bound, and a bound cannot rot the way a mirror does: set it
 * too high and you waste a few bytes of allocation bitmap; set it too low and
 * the extent cannot be fully used, which the floor assertion below turns into a
 * build failure rather than silent lost space.
 *
 * This replaced a hand-tuned per-platform table (910/387/934/420/293, retuned
 * twice in v0.06 alone) that had to be recomputed by hand whenever a code
 * window moved -- diagnosed as defect D-e, "static geometry mirrors rot", in
 * the design of record, whose §4.6 prescribed exactly this.
 *
 * 2048 slots covers ~8 MB of store at a 4 KB slot and costs 256 bytes of
 * bitmap; the largest shipped extent needs 903.
 */
#  if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
      defined(PLATFORM_NORDIC)
#    define TIKU_TFS_MAX_SLOTS  2048
#  else
#    define TIKU_TFS_MAX_SLOTS  32      /* msp430/host: 4 bytes of bitmap */
#  endif
#endif

#ifndef TIKU_TFS_MIN_SLOTS
/*
 * FLOOR -- slots every board in this class is GUARANTEED to have.
 *
 * Mount refuses an extent that cannot reach this, so a shrunken carve fails
 * loudly at boot instead of yielding a store too small for its tenants. Clients
 * that must prove a worst case at BUILD time assert against this, never against
 * the derived count, which is not a compile-time value any more.
 */
#  if defined(AM_PART_APOLLO510) || defined(PLATFORM_RP2350)
#    define TIKU_TFS_MIN_SLOTS  512
#  elif defined(PLATFORM_AMBIQ) || defined(TIKU_DEVICE_NRF54LM20A) || \
        defined(TIKU_DEVICE_NRF54LM20B)
#    define TIKU_TFS_MIN_SLOTS  256
#  elif defined(PLATFORM_NORDIC)
#    define TIKU_TFS_MIN_SLOTS  192
#  else
#    define TIKU_TFS_MIN_SLOTS  16
#  endif
#endif

#ifndef TIKU_TFS_SLOT_DATA
/*
 * Per-file ceiling.  4 KB on every part backed by the carved NVM region --
 * MRAM, RRAM and Flash alike -- so one number describes the target class.
 * (Nordic ran at 512 B until v0.06; that was a capacity guess made when the
 * RRAM FS extent was 256 KB, never a property of the silicon, and it capped
 * files at an MSP430 figure on a part with megabytes of region.)
 *
 * On RP2350 the slot is sized so that length+content == one 4 KB flash erase
 * sector (see TIKU_TFS_SECT), so a power cut during one file's write cannot
 * reach a neighbour file's sector.  MSP430 FRAM and host keep 512 B: their
 * store is a modest in-place FRAM array, not a region extent.
 */
#  if defined(PLATFORM_RP2350)
#    define TIKU_TFS_SLOT_DATA  4092   /**< 4 B length + 4092 = 4096 (one sector) */
#  elif defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
#    define TIKU_TFS_SLOT_DATA  4096   /**< MRAM/RRAM: no erase granule */
#  else
#    define TIKU_TFS_SLOT_DATA  512    /**< max bytes per file */
#  endif
#endif

/* On-NVM layout alignment = the medium's erase granule, so each data slot owns
 * whole erase sectors and the directory/data boundary is sector-aligned; a power
 * cut during one file's write then cannot corrupt a neighbor file. Byte/word-
 * writable NVM (FRAM/MRAM, no erase) uses plain 4-byte alignment. */
#ifndef TIKU_TFS_SECT
#  if defined(PLATFORM_RP2350)
#    define TIKU_TFS_SECT  4096u
#  else
#    define TIKU_TFS_SECT  4u
#  endif
#endif
#define TIKU_TFS_ALIGN(n, a)   (((n) + (a) - 1u) & ~((a) - 1u))

/* Superblock: magic + one u32 per geometry parameter (see tiku_tfs.c).  Named
 * here because the directory starts immediately after it, so every downstream
 * offset depends on it -- it used to be a bare 8 written into DATA_OFF below,
 * which is exactly the kind of duplicated constant that drifts. */
#define TIKU_TFS_SB_BYTES    (8u * 4u)
#define TIKU_TFS_DE_BYTES    (((8u + TIKU_TFS_NAME_MAX + 3u) & ~3u))
#define TIKU_TFS_SLOT_BYTES  TIKU_TFS_ALIGN(4u + TIKU_TFS_SLOT_DATA, TIKU_TFS_SECT)

/**
 * @brief Where the data region starts for a store holding @p n files.
 *
 * The directory sits between the superblock and the data, so this moves with
 * the file count -- which is why the count is recorded in the superblock and a
 * store must never be parsed under a different one.
 */
#define TIKU_TFS_DATA_OFF_FOR(n)                                                \
    TIKU_TFS_ALIGN(TIKU_TFS_SB_BYTES + TIKU_TFS_DE_BYTES * (unsigned)(n),       \
                   TIKU_TFS_SECT)

/**
 * @brief Bytes of NVM a store holding @p n files occupies.
 *
 * The INVERSE of what mount does: mount is handed an extent and derives the
 * largest n that fits, while a caller that owns its own backing memory (the
 * MSP430 FRAM array, the host test harness) starts from the n it wants and
 * sizes the array with this.  Both directions use the same layout, so a store
 * sized by this always derives exactly @p n back.
 */
#define TIKU_TFS_EXTENT_FOR_SLOTS(n)                                            \
    (TIKU_TFS_DATA_OFF_FOR(n) + TIKU_TFS_SLOT_BYTES * (unsigned)((n) + 1u))

/*---------------------------------------------------------------------------*/
/* STATUS CODES                                                              */
/*---------------------------------------------------------------------------*/

typedef enum {
    TFS_OK            =  0,
    TFS_ERR_INVAL     = -1,  /**< NULL arg / not mounted */
    TFS_ERR_NOSPACE   = -2,  /**< no free directory slot / data slot */
    TFS_ERR_TOOBIG    = -3,  /**< content larger than TIKU_TFS_SLOT_DATA */
    TFS_ERR_NAMELEN   = -4,  /**< name empty or >= TIKU_TFS_NAME_MAX */
    TFS_ERR_EXISTS    = -5,  /**< file already exists (create) */
    TFS_ERR_NOTFOUND  = -6,  /**< no such file */
    TFS_ERR_IO        = -7,  /**< backend write failed */
    TFS_ERR_CORRUPT   = -8,  /**< on-NVM structure failed validation */
    TFS_ERR_BUSY      = -9   /**< another writer holds the store (see below) */
} tfs_err_t;

/*---------------------------------------------------------------------------*/
/* MOUNT STATE (in SRAM; rebuilt at mount, never persisted)                  */
/*---------------------------------------------------------------------------*/

/* The store carries one slot beyond its file count.  That guaranteed a shadow
 * for overwrite when every file was exactly one slot; with spans it only
 * guarantees one for a SPAN-1 file, so a spanned replace can still return
 * NOSPACE with free bytes on the clock (see TIKU_TFS_FILE_MAX below).  The
 * count itself is derived at mount and lives in tiku_tfs_t. */

/**
 * @brief Largest file the store can hold: every data slot in one run.
 *
 * A file owns a run of contiguous slots and only the FIRST slot's length word
 * is metadata, so a span of n holds n*SLOT_BYTES-4 bytes -- and a one-slot
 * file is exactly the n==1 case (TIKU_TFS_SLOT_DATA).  This is the ceiling in
 * principle; in practice a write also needs a free run of that length, and a
 * REPLACE needs one on top of what the file already occupies (the new run must
 * exist before the dirent flips to it).  So TFS_ERR_NOSPACE is now reachable
 * for large files even when the total free byte count looks sufficient --
 * fragmentation is real once files span.
 */
#define TIKU_TFS_FILE_MAX \
    ((size_t)TIKU_TFS_MAX_SLOTS * TIKU_TFS_SLOT_BYTES - 4u)

/**
 * @brief Largest file EVERY board in this class is guaranteed to accept.
 *
 * TIKU_TFS_FILE_MAX above is what this build can ADDRESS; this is what it can
 * PROMISE, derived from the floor rather than the ceiling.  A client proving at
 * build time that its worst-case object fits must assert against this one --
 * the addressable ceiling would pass on a board whose carve cannot deliver it.
 */
#define TIKU_TFS_FILE_MAX_GUARANTEED \
    ((size_t)TIKU_TFS_MIN_SLOTS * TIKU_TFS_SLOT_BYTES - 4u)

/**
 * @brief Slots a file of @p n content bytes occupies -- a CONSTANT expression.
 *
 * The closed form of the allocator's run_span_for(): since a span of s holds
 * s*SLOT_BYTES-4 bytes, s = ceil((n + 4) / SLOT_BYTES).  run_span_for() is a
 * loop and therefore unusable in a _Static_assert, which is exactly where a
 * client needs this -- to prove at build time that its worst-case object fits
 * the store alongside the other tenants.
 */
#define TIKU_TFS_SPAN_FOR(n) \
    (((size_t)(n) + 4u + TIKU_TFS_SLOT_BYTES - 1u) / TIKU_TFS_SLOT_BYTES)

typedef struct tiku_tfs {
    tiku_nvm_backend_t *be;
    /* SINGLE-WRITER INTERLOCK.  Set while a streamed write is open, so any
     * other write REFUSES with TFS_ERR_BUSY instead of interleaving into the
     * staged run or the directory.  See tiku_tfs_open_w() for why this refuses
     * rather than blocks. */
    uint8_t  wr_open;
    /* DERIVED AT MOUNT from be->size -- the extent the linker actually carved.
     * They are not compile-time values any more, which is the point: nothing in
     * C mirrors the carve, so nothing can disagree with it. */
    uint16_t nfiles;      /**< directory entries this store holds        */
    uint16_t nslots;      /**< data slots = nfiles + 1 (overwrite shadow) */
    uint32_t data_off;    /**< byte offset of slot 0                      */
    /* Sized by the CEILING, not the derived count: a bound may be generous
     * (this costs bytes) but must never be short (that would be an overflow). */
    uint8_t  slot_used[(TIKU_TFS_MAX_SLOTS + 7u) / 8u];
    uint8_t  mounted;
} tiku_tfs_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief NVM bytes the store needs (size the backend region to at least this). */
size_t tiku_tfs_region_size(void);

/** @brief Mount an existing store; format if the superblock is absent/invalid.
 *         @return TFS_OK or a negative tfs_err_t. */
int tiku_tfs_mount(tiku_tfs_t *fs, tiku_nvm_backend_t *be);

/** @brief Wipe and (re)format the store. */
int tiku_tfs_format(tiku_tfs_t *fs);

/** @brief Create an empty file. TFS_ERR_EXISTS if it already exists. */
int tiku_tfs_create(tiku_tfs_t *fs, const char *name);

/** @brief Create-or-overwrite @p name with @p len bytes (atomic overwrite). */
int tiku_tfs_write(tiku_tfs_t *fs, const char *name,
                   const void *data, size_t len);

/**
 * @brief A write in progress (see tiku_tfs_open_w).  Caller-allocated.
 *
 * Fields are internal.  The staged run is reserved in RAM only, so a power
 * cut mid-stream needs no cleanup: the next mount rebuilds the allocation map
 * from the live directory, which never referenced the staged run.
 */
typedef struct {
    tiku_tfs_t *fs;
    unsigned    first;                  /**< first slot of the staged run   */
    unsigned    span;                   /**< slots reserved                 */
    size_t      cap;                    /**< content capacity of the run    */
    size_t      off;                    /**< bytes appended so far          */
    int         active;                 /**< 1 between open_w and commit    */
    char        name[TIKU_TFS_NAME_MAX];
} tiku_tfs_wr_t;

/**
 * @brief Begin a streamed write of @p name, reserving room for @p max_len.
 *
 * Lets a file be produced in bounded chunks instead of from one whole buffer
 * -- required where the payload is larger than available RAM (a 258 KB saved
 * BASIC program on a 240 KB part) or arrives incrementally (a model over
 * serial).  Nothing in the directory changes until tiku_tfs_commit(), so the
 * previous content stands until the moment it is replaced.
 *
 * @p max_len is a RESERVATION, not a promise: commit records however many
 * bytes were actually appended, but keeps the reserved span.  Declaring a
 * stable maximum is what makes a repeatedly-replaced file ping-pong between
 * two equal-length runs instead of leaving ragged holes.
 *
 * @return TFS_OK, or TFS_ERR_NOSPACE / _TOOBIG / _NAMELEN / _INVAL.
 */
int tiku_tfs_open_w(tiku_tfs_t *fs, tiku_tfs_wr_t *w,
                    const char *name, size_t max_len);

/** @brief Append @p len bytes to an open write. @return TFS_OK or an error. */
int tiku_tfs_write_chunk(tiku_tfs_wr_t *w, const void *data, size_t len);

/**
 * @brief Publish an open write: length word, then ONE atomic dirent update.
 *
 * The old run is reclaimed only after the dirent points at the new one.
 * @return TFS_OK, or a negative error (the write stays open on failure).
 */
int tiku_tfs_commit(tiku_tfs_wr_t *w);

/** @brief Discard an open write; the file keeps its previous content. */
void tiku_tfs_abort(tiku_tfs_wr_t *w);

/** @brief Copy a file's content into @p buf; @p out_len gets the true length. */
int tiku_tfs_read(tiku_tfs_t *fs, const char *name,
                  void *buf, size_t max, size_t *out_len);

/** @brief Zero-copy read: point @p p straight into the NVM region (read-only). */
int tiku_tfs_map(tiku_tfs_t *fs, const char *name,
                 const void **p, size_t *len);

/** @brief Delete a file. */
int tiku_tfs_delete(tiku_tfs_t *fs, const char *name);

/** @brief Stat a file's length. */
int tiku_tfs_stat(tiku_tfs_t *fs, const char *name, size_t *len);

/** @brief Per-file callback for tiku_tfs_list(). */
typedef void (*tiku_tfs_iter_cb)(const char *name, size_t len, void *ctx);

/** @brief Enumerate live files. @return the count, or a negative tfs_err_t. */
int tiku_tfs_list(tiku_tfs_t *fs, tiku_tfs_iter_cb cb, void *ctx);

/**
 * @brief Enumerate the immediate children under @p prefix, presenting the flat
 *        store as a directory tree (path-as-name).
 *
 * Files in the directory come back by leaf name; deeper paths contribute their
 * first path segment once, suffixed with '/' so folders are distinguishable.
 * @p prefix is "" (store root) or e.g. "logs/".  @return the child count.
 */
int tiku_tfs_list_dir(tiku_tfs_t *fs, const char *prefix,
                      tiku_tfs_iter_cb cb, void *ctx);

/** @brief Number of free directory slots. */
size_t tiku_tfs_free_files(tiku_tfs_t *fs);

#endif /* TIKU_TFS_H_ */
