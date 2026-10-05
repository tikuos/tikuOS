/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.h - Tiku File Store: bounded, power-cut-safe files over NVM.
 *
 * Flat namespace, whole-file I/O, fixed-size slots; a file may span several
 * contiguous slots.  Every commit is a single aligned 32-bit write, so a power
 * cut leaves the previous contents rather than a torn file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TFS_H_
#define TIKU_TFS_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_nvm_backend.h"

/*---------------------------------------------------------------------------*/
/* COMPILE-TIME LIMITS                                                       */
/*---------------------------------------------------------------------------*/
/* Board-aware defaults, resolved in the build that includes this header. */

#ifndef TIKU_TFS_NAME_MAX
#define TIKU_TFS_NAME_MAX   24      /**< max filename length incl. NUL */
#endif
#ifndef TIKU_TFS_MAX_SLOTS
/**
 * @brief Ceiling: the largest slot count this build can address.
 *
 * The store is sized at mount from be->size; tfs_fit() clamps it to this, so
 * an extent beyond the ceiling goes partly unused.  2048 slots address 8 MB
 * of 4 KB slots for 256 bytes of allocation bitmap.
 */
#  if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1) || defined(PLATFORM_ESP32C61)
#    define TIKU_TFS_MAX_SLOTS  2048
#  else
#    define TIKU_TFS_MAX_SLOTS  32      /* msp430/host: 4 bytes of bitmap */
#  endif
#endif

#ifndef TIKU_TFS_MIN_SLOTS
/**
 * @brief Floor: slots every board in this class has.
 *
 * Mount refuses an extent too small for it (TFS_ERR_NOSPACE).  A build-time
 * fit check asserts against this, not against the count derived at mount.
 */
#  if defined(AM_PART_APOLLO510) || defined(PLATFORM_RP2350) || \
      defined(PLATFORM_STM32N6) || defined(PLATFORM_ESP32C61)
#    define TIKU_TFS_MIN_SLOTS  512
#  elif defined(PLATFORM_AMBIQ) || defined(TIKU_DEVICE_NRF54LM20A) || \
        defined(TIKU_DEVICE_NRF54LM20B)
#    define TIKU_TFS_MIN_SLOTS  256
#  elif defined(PLATFORM_NORDIC)
#    define TIKU_TFS_MIN_SLOTS  192
#  elif defined(PLATFORM_RA8P1)
     /* Under what the RA8P1 carve mounts, with margin. */
#    define TIKU_TFS_MIN_SLOTS  96
#  else
#    define TIKU_TFS_MIN_SLOTS  16
#  endif
#endif

#ifndef TIKU_TFS_SLOT_DATA
/**
 * @brief Payload bytes of one slot; a file may span several slots
 *        (TIKU_TFS_FILE_MAX), except on MSP430, which keeps one per file.
 *
 * On flash parts a slot, its 4-byte length word included, is one 4 KB erase
 * sector (TIKU_TFS_SECT), so a power cut during one file's write cannot reach
 * a neighbour's sector.  MSP430 and host use 512-byte slots.
 */
#  if defined(PLATFORM_RP2350) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_ESP32C61)
#    define TIKU_TFS_SLOT_DATA  4092   /**< + 4 B length = one 4 KB sector */
#  elif defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC) || \
       defined(PLATFORM_RA8P1)
#    define TIKU_TFS_SLOT_DATA  4096   /**< MRAM/RRAM: no erase granule */
#  else
#    define TIKU_TFS_SLOT_DATA  512    /**< MSP430 and host */
#  endif
#endif

/**
 * @brief Alignment of the on-NVM layout: the medium's erase granule.
 *
 * Each data slot owns whole erase sectors and the directory/data boundary is
 * sector-aligned, so a power cut during one file's write cannot corrupt a
 * neighbour.  Byte-writable media (FRAM, MRAM, RRAM) use 4-byte alignment.
 */
#ifndef TIKU_TFS_SECT
#  if defined(PLATFORM_RP2350) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_ESP32C61)
#    define TIKU_TFS_SECT  4096u
#  else
#    define TIKU_TFS_SECT  4u
#  endif
#endif
/** @brief Round @p n up to a multiple of @p a, a power of two. */
#define TIKU_TFS_ALIGN(n, a)   (((n) + (a) - 1u) & ~((a) - 1u))

/**
 * @brief Superblock bytes: the magic and seven geometry words (see
 *        tiku_tfs.c).  The directory starts right after it.
 */
#define TIKU_TFS_SB_BYTES    (8u * 4u)
/** @brief Bytes of one directory entry: gate, run word and name, 4-aligned. */
#define TIKU_TFS_DE_BYTES    (((8u + TIKU_TFS_NAME_MAX + 3u) & ~3u))
/**
 * @brief Bytes of one data slot: room for a 4-byte length word and
 *        TIKU_TFS_SLOT_DATA, aligned to TIKU_TFS_SECT.
 */
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
 * The inverse of mount, which derives the largest n that fits an extent.  A
 * caller owning its backing memory (MSP430 FRAM, host harness) starts from n
 * instead; both use the same layout, so a store sized here derives @p n back.
 */
#define TIKU_TFS_EXTENT_FOR_SLOTS(n)                                            \
    (TIKU_TFS_DATA_OFF_FOR(n) + TIKU_TFS_SLOT_BYTES * (unsigned)((n) + 1u))

/*---------------------------------------------------------------------------*/
/* STATUS CODES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Result codes: TFS_OK, or a negative error. */
typedef enum {
    TFS_OK            =  0,
    TFS_ERR_INVAL     = -1,  /**< NULL arg / not mounted */
    TFS_ERR_NOSPACE   = -2,  /**< no free directory slot / data slot */
    TFS_ERR_TOOBIG    = -3,  /**< past the reservation or over the file limit */
    TFS_ERR_NAMELEN   = -4,  /**< name empty or >= TIKU_TFS_NAME_MAX */
    TFS_ERR_EXISTS    = -5,  /**< file already exists (create) */
    TFS_ERR_NOTFOUND  = -6,  /**< no such file */
    TFS_ERR_IO        = -7,  /**< backend write failed */
    TFS_ERR_CORRUPT   = -8,  /**< on-NVM structure failed validation */
    TFS_ERR_BUSY      = -9,  /**< a stream or a read lease holds the store */
    TFS_ERR_NOSTORE   = -10, /**< no store header; tiku_tfs_probe() says why */
    TFS_ERR_GEOMETRY  = -11  /**< store for another extent or format version */
} tfs_err_t;

/** @brief What the first bytes of an extent say about a store there. */
typedef enum {
    TFS_PROBE_COMPATIBLE = 0, /**< mounts as it is                            */
    TFS_PROBE_BLANK,          /**< full probe: entire extent is 00 or FF      */
    TFS_PROBE_TORN,           /**< header gone, geometry or entries remain    */
    TFS_PROBE_GEOMETRY,       /**< formatted for an extent of another size    */
    TFS_PROBE_VERSION,        /**< written in another format version          */
    TFS_PROBE_TOOSMALL,       /**< the extent cannot hold the smallest store  */
    TFS_PROBE_UNKNOWN         /**< nonblank bytes with no recognizable store */
} tfs_probe_kind_t;

/** @brief One probe's findings; every field is read, nothing is written. */
typedef struct {
    tfs_probe_kind_t kind;
    uint32_t version;   /**< recorded format version, 0 without a header  */
    uint32_t nfiles;    /**< recorded file count, or derived without one  */
    uint16_t live;      /**< live directory entries found (full probe)    */
    uint32_t bytes;     /**< their content bytes, for a compatible store  */
} tiku_tfs_probe_t;

/** @brief A store header found by tiku_tfs_locate(). */
typedef struct {
    uint32_t         off;   /**< byte offset of its header in the region */
    tfs_probe_kind_t kind;  /**< COMPATIBLE, TORN, GEOMETRY or VERSION   */
} tiku_tfs_cand_t;

/** @brief Granularity at which a store may begin inside a carved region. */
#define TIKU_TFS_LOCATE_STEP  4096u

/*---------------------------------------------------------------------------*/
/* MOUNT STATE                                                               */
/*---------------------------------------------------------------------------*/

/* Mount state lives in SRAM: rebuilt at mount, never persisted.
 *
 * The store carries one data slot beyond its file count: the shadow that lets
 * a one-slot file be replaced.  Replacing a spanned file needs a free run as
 * long as the new content, so it can return TFS_ERR_NOSPACE while enough slots
 * are free but scattered.  The counts are derived at mount (tiku_tfs_t). */

/**
 * @brief Largest file this build can address: TIKU_TFS_MAX_SLOTS in one run.
 *
 * Only the first slot's length word is metadata, so a span of n holds
 * n*SLOT_BYTES-4 bytes.  A write also needs a free run that long, and a
 * replace needs one beside the file's own; MSP430 keeps one slot per file.
 */
#define TIKU_TFS_FILE_MAX \
    ((size_t)TIKU_TFS_MAX_SLOTS * TIKU_TFS_SLOT_BYTES - 4u)

/**
 * @brief Largest file every board in this class is guaranteed to accept.
 *
 * Derived from the floor, where TIKU_TFS_FILE_MAX comes from the ceiling.  A
 * build-time fit check asserts against this one: the ceiling would pass on a
 * board whose carve cannot hold it.
 */
#define TIKU_TFS_FILE_MAX_GUARANTEED \
    ((size_t)TIKU_TFS_MIN_SLOTS * TIKU_TFS_SLOT_BYTES - 4u)

/**
 * @brief Slots a file of @p n content bytes occupies, as a constant
 *        expression.
 *
 * Closed form of the allocator's run_span_for(): ceil((n + 4) / SLOT_BYTES).
 * run_span_for() is a loop, so it cannot appear in a _Static_assert.
 */
#define TIKU_TFS_SPAN_FOR(n) \
    (((size_t)(n) + 4u + TIKU_TFS_SLOT_BYTES - 1u) / TIKU_TFS_SLOT_BYTES)

/** @brief Mount state of one store: its backend and the derived geometry. */
typedef struct tiku_tfs {
    tiku_nvm_backend_t *be;
    /** Set while a streamed write is open: every other write returns
     *  TFS_ERR_BUSY rather than interleave with it (see tiku_tfs_open_w()). */
    uint8_t  wr_open;
    /* Derived at mount from be->size. */
    uint16_t nfiles;      /**< directory entries this store holds        */
    uint16_t nslots;      /**< data slots = nfiles + 1 (overwrite shadow) */
    uint32_t data_off;    /**< byte offset of slot 0                      */
    /** In-RAM allocation map, one bit per slot, sized by the ceiling because
     *  nslots is only known at mount. */
    uint8_t  slot_used[(TIKU_TFS_MAX_SLOTS + 7u) / 8u];
    uint8_t  mounted;
} tiku_tfs_t;

/**
 * @brief Build the read-lease API; on by default only with
 *        TIKU_MEM_RECLAIM_ENABLE.
 */
#ifndef TIKU_TFS_HOLD_ENABLE
#if defined(TIKU_MEM_RECLAIM_ENABLE) && TIKU_MEM_RECLAIM_ENABLE
#define TIKU_TFS_HOLD_ENABLE 1
#else
#define TIKU_TFS_HOLD_ENABLE 0
#endif
#endif

/**
 * @brief A read lease: while one is held, every write, delete, format or
 *        mount of the store returns TFS_ERR_BUSY.
 *
 * Held while a consumer rebuilds RAM from mapped files; several readers may
 * hold one store.  A lease does not guard against raw NVM access or reboot.
 */
typedef struct tiku_tfs_hold {
    tiku_tfs_t *fs;                /**< store held; NULL once released */
    struct tiku_tfs_hold *next;    /**< next lease on the held list    */
} tiku_tfs_hold_t;

/**
 * @brief Take a read lease on a mounted store.
 *
 * @note Keep @p hold in stable storage until tiku_tfs_release().
 * @return TFS_OK; TFS_ERR_BUSY while a streamed write is open or @p hold is
 *         already held; TFS_ERR_INVAL if unmounted or TIKU_TFS_HOLD_ENABLE=0.
 */
int tiku_tfs_hold(tiku_tfs_t *fs, tiku_tfs_hold_t *hold);

/**
 * @brief Release a read lease.
 *
 * @return TFS_OK, or TFS_ERR_INVAL if @p hold is not held (always with
 *         TIKU_TFS_HOLD_ENABLE=0).
 */
int tiku_tfs_release(tiku_tfs_hold_t *hold);

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief NVM bytes the smallest store needs; size the backend region to at
 *        least this.
 */
size_t tiku_tfs_region_size(void);

/**
 * @brief Mount the store @p be holds.  Never formats.
 *
 * A missing header is TFS_ERR_NOSTORE and a header for another geometry or
 * version is TFS_ERR_GEOMETRY; tiku_tfs_probe() tells the caller which case
 * it met, and only an explicit tiku_tfs_format() or tiku_tfs_init() writes.
 *
 * @return TFS_OK or a negative tfs_err_t.
 */
int tiku_tfs_mount(tiku_tfs_t *fs, tiku_nvm_backend_t *be);

/** @brief Wipe and reformat a store bound by a previous mount or init. */
int tiku_tfs_format(tiku_tfs_t *fs);

/** @brief Bind @p be and format it: the explicit way to create a store. */
int tiku_tfs_init(tiku_tfs_t *fs, tiku_nvm_backend_t *be);

/**
 * @brief Classify the store at the start of @p be without writing anything.
 *
 * Reads the header and counts live directory entries, so a store that lost
 * only its header reads as torn and a report can count the files at stake.
 *
 * @return TFS_OK, or TFS_ERR_INVAL for a NULL argument.
 */
int tiku_tfs_probe(const tiku_nvm_backend_t *be, tiku_tfs_probe_t *out);

/**
 * @brief List every store header in @p region, at @p step granularity.
 *
 * Headers only: a candidate is a header at an offset whose remaining extent
 * could hold a store.  Fills at most @p max entries of @p out.
 *
 * @return The number of candidates found, or TFS_ERR_INVAL.
 */
int tiku_tfs_locate(const tiku_nvm_backend_t *region, size_t step,
                    tiku_tfs_cand_t *out, int max);

/**
 * @brief Whether a store may be created at @p base_off without asking.
 *
 * True only when the whole region is uniformly 0x00 or 0xFF and the extent
 * at the base can hold a store; missing headers alone are not blank media.
 */
int tiku_tfs_may_provision(const tiku_nvm_backend_t *region, size_t base_off,
                           size_t step);

/** @brief A short name for a probe kind, for reports. */
const char *tiku_tfs_probe_name(tfs_probe_kind_t kind);

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
 * Writes a file larger than RAM in bounded chunks; nothing in the directory
 * changes until tiku_tfs_commit().  @p max_len is reserved while the write is
 * open, and commit keeps only the slots the appended bytes reach.
 *
 * @return TFS_OK, or TFS_ERR_NOSPACE / _TOOBIG / _NAMELEN / _INVAL / _BUSY.
 */
int tiku_tfs_open_w(tiku_tfs_t *fs, tiku_tfs_wr_t *w,
                    const char *name, size_t max_len);

/** @brief Append @p len bytes to an open write. @return TFS_OK or an error. */
int tiku_tfs_write_chunk(tiku_tfs_wr_t *w, const void *data, size_t len);

/**
 * @brief Publish an open write: length word, then one atomic dirent update.
 *
 * The dirent names only the slots the content reached.  The old run and the
 * reservation's unused tail are reclaimed only after it does.
 * @return TFS_OK, or a negative error (the write stays open on failure).
 */
int tiku_tfs_commit(tiku_tfs_wr_t *w);

/** @brief Discard an open write; the file keeps its previous content. */
void tiku_tfs_abort(tiku_tfs_wr_t *w);

/** @brief Copy a file into @p buf; @p out_len gets its true length. */
int tiku_tfs_read(tiku_tfs_t *fs, const char *name,
                  void *buf, size_t max, size_t *out_len);

/** @brief Zero-copy read: point @p p into the NVM region (read-only). */
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

/** @brief Data slots in use, an open write's reservation included. */
size_t tiku_tfs_used_slots(tiku_tfs_t *fs);

#endif /* TIKU_TFS_H_ */
