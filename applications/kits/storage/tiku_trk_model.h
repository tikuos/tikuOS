/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_model.h - the Model and the backend seam.
 *
 * Tracker's Model is the one place that knows what kind of thing a pose wraps;
 * everything above it asks questions (IsContainer, IsWritable, Kind) rather
 * than testing a backend.  That is what lets local files and a device
 * namespace be browsed by the same view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_MODEL_H_
#define TIKU_TRK_MODEL_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_TRK_NAME_MAX  128
#define TIKU_TRK_PATH_MAX  512
#define TIKU_TRK_TYPE_MAX   48
#define TIKU_TRK_META_MAX   72

/**
 * @brief What a Model is.
 *
 * Directories on a device are VIRTUAL_DIR, not DIRECTORY: they cannot be
 * created in, renamed or pasted into, and the whole read-only story for /sys,
 * /dev and /proc follows from that one answer (the alternative -- reusing
 * DIRECTORY -- silently offers New Folder on /sys).  A dynamic store like
 * /data IS a real DIRECTORY.
 */
typedef enum {
    TIKU_TRK_KIND_UNKNOWN = 0,
    TIKU_TRK_KIND_FILE,          /* a local file                            */
    TIKU_TRK_KIND_DIRECTORY,     /* a local dir, or a device dynamic store  */
    TIKU_TRK_KIND_SYMLINK,
    TIKU_TRK_KIND_VOLUME,        /* a mount root, or a device root          */
    TIKU_TRK_KIND_ROOT,          /* the synthetic Disks root                */
    TIKU_TRK_KIND_VIRTUAL_DIR,   /* a device namespace directory            */
    TIKU_TRK_KIND_DEVICE_NODE,   /* a device namespace leaf: a live value   */
    TIKU_TRK_KIND_QUERY,
    /* A print destination.  Tracker gives these their own art and so does
     * the icon set; without the kind the blob is never asked for. */
    TIKU_TRK_KIND_PRINTER,
    TIKU_TRK_KIND_TRASH,
    TIKU_TRK_KIND_DESKTOP,
} tiku_trk_kind_t;

/** @brief Permission bits, from stat() or from the manifest's perm column. */
#define TIKU_TRK_P_READ   0x1u
#define TIKU_TRK_P_WRITE  0x2u
#define TIKU_TRK_P_EXEC   0x4u

/**
 * @brief What the backend told us about a node, as facts rather than verdicts.
 *
 * ONE copy of the manifest's columns lives here; permission answers are
 * derived from it and memoised elsewhere, never duplicated -- two copies
 * refreshed on different triggers is how menu enablement goes silently wrong.
 */
typedef struct {
    unsigned perm;                        /* TIKU_TRK_P_*                   */
    char     req_cap[24];                 /* "-", "hw", "sys", ...          */
    char     meta[TIKU_TRK_META_MAX];     /* typed descriptor, or "-"       */
    int      cap_known;                   /* 0 when the manifest was cut    */
    /* A link whose target is gone.  It is still a link and still an entry:
     * the row stays and says so, rather than disappearing as though the
     * link itself had been deleted. */
    int      link_broken;
    /* The raw mode word, when the store HAS one (MA-031): the Permissions
     * column renders the classic ten-character string from it.  A store
     * with no mode bits leaves mode_known 0 and the column falls back to
     * the three access answers this user actually has. */
    unsigned mode;
    int      mode_known;
    /* A Trash that has something in it.  Carried as a FACT rather than
     * recomputed at draw time: the icon, the menu item's enablement and
     * the delete question all ask the same question, and a directory read
     * per draw to answer it would be a read per frame. */
    int      trash_full;
    /* A VOLUME's capacity.  Carried as facts for the same reason the
     * Trash's fullness is: the space bar, the Kind column and the free-
     * space question all ask it, and a statvfs per draw would be one per
     * frame.  Both zero on anything that is not a volume. */
    uint64_t total, avail;
    /* Which filesystem the entry is ON, and what that filesystem allows.
     * Carried rather than re-derived: "every window showing something on
     * the volume that just left" cannot be asked of a path, and re-stat()ing
     * for st_dev on every drop question is a syscall per question. */
    uint64_t dev;
    unsigned vol_flags;
    int64_t  size;
    int64_t  mtime;
    /* Whether the node carries a picture of its own, asked ONCE when the
     * entry is taken in rather than on every draw.  Unasked is a distinct
     * answer from none: a zeroed model has not been probed, and treating
     * that as "no icon" is how a file's own art silently never appears. */
    int      icon_hint;
} tiku_trk_facts_t;

/** @brief What facts.vol_flags says about the volume the entry is on. */
#define TIKU_TRK_VOL_READ_ONLY 0x1u
#define TIKU_TRK_VOL_BOOT      0x2u
#define TIKU_TRK_VOL_REMOVABLE 0x4u
#define TIKU_TRK_VOL_SHARED    0x8u   /* a network share, not a disk      */

/** @brief The answers facts.icon_hint can hold. */
#define TIKU_TRK_ICONHINT_UNASKED 0
#define TIKU_TRK_ICONHINT_NONE    1
#define TIKU_TRK_ICONHINT_ATTR    2  /* an icon attribute of its own       */
#define TIKU_TRK_ICONHINT_THUMB   3  /* its own picture IS its icon        */

typedef struct tiku_trk_backend tiku_trk_backend_t;

/**
 * @brief One change the backend reports: what happened, and to which node.
 *
 * The op is not decoration.  A removal and a value move demand different
 * work, and a caller that keeps only the identity treats them alike -- which
 * shows as a deleted row staying on screen.
 */
typedef struct {
    uint64_t id;
    int      op;      /* 0 changed, 1 created, 2 removed, 3 moved          */
} tiku_trk_change_t;

#define TIKU_TRK_CH_CHANGED 0
#define TIKU_TRK_CH_CREATED 1
#define TIKU_TRK_CH_REMOVED 2
#define TIKU_TRK_CH_MOVED   3

/** @brief One entry, whatever backend it came from. */
typedef struct {
    char                 name[TIKU_TRK_NAME_MAX];
    char                 path[TIKU_TRK_PATH_MAX];
    char                 type[TIKU_TRK_TYPE_MAX];   /* MIME or tiku-* type  */
    tiku_trk_kind_t      kind;
    tiku_trk_facts_t     facts;
    tiku_trk_backend_t  *backend;                   /* not owned            */
    uint64_t             node_id;                   /* inode, or a hash     */
    unsigned             generation;                /* bumped on any change */
} tiku_trk_model_t;

/*---------------------------------------------------------------------------*/
/* Questions the view asks -- never "which backend is this"                  */
/*---------------------------------------------------------------------------*/

/** @brief Can it hold other entries (any kind of directory or volume). */
int tiku_trk_model_is_container(const tiku_trk_model_t *m);

/** @brief Can entries be created, renamed or pasted inside it. */
int tiku_trk_model_is_mutable_container(const tiku_trk_model_t *m);

/** @brief Is its value writable (a file, or a writable device node). */
int tiku_trk_model_is_writable(const tiku_trk_model_t *m);

/**
 * @brief The special display name presented to people, never a path key.
 *
 * Model.name remains the real entry name for filesystem operations and
 * round-tripping.  Views use this answer so special roots need no local
 * substitute labels.
 */
const char *tiku_trk_model_display_name(const tiku_trk_model_t *m);

/**
 * @brief Walk the type table (Q-015).
 *
 * The port has no type DATABASE: what a type means comes from a compiled-in
 * table, so "every type the system knows" is exactly this list.  A menu that
 * offered more would be offering types nothing can recognise.
 *
 * @param i     Row index, from zero.
 * @param label Receives the human name ("Text file"), or NULL.
 * @return the type string, or NULL past the end.
 */
const char *tiku_trk_model_type_at(int i, const char **label);

/**
 * @brief Whether @p type is the type @p want asks for (Q-004).
 *
 * A supertype admits its members -- picking "text/" finds
 * "text/x-source-code" -- which is the same widening the icon and kind
 * lookups already do, and the reason a menu of a dozen entries is useful
 * at all.
 */
int tiku_trk_model_type_is(const char *type, const char *want);

/** @brief Human "Kind" column text. */
const char *tiku_trk_model_kind_string(const tiku_trk_model_t *m);

/**
 * @brief Which icon of the set represents it.
 *
 * Tracker resolves an icon from the node's own BEOS:ICON attribute, then its
 * MIME type, then a per-kind default; we have only the last two steps, so a
 * node cannot yet carry bespoke art.
 *
 * @return A name for tiku_trk_icons_bitmap(); never NULL.
 */
const char *tiku_trk_model_icon_name(const tiku_trk_model_t *m);

/*---------------------------------------------------------------------------*/
/* The backend seam                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Receives one entry during a listing; return non-zero to stop. */
typedef int (*tiku_trk_entry_fn)(const tiku_trk_model_t *m, void *ctx);

/**
 * @brief What every backend implements.
 *
 * Listing is a callback rather than an array because a directory read is
 * incremental: Tracker shows the first poses while the rest are still
 * arriving, and a slow device must not block the caller for its whole tree.
 */
typedef struct {
    const char *name;

    /** @brief Fill @p out for one path.  0 on success. */
    int (*stat)(tiku_trk_backend_t *b, const char *path,
                tiku_trk_model_t *out);

    /** @brief Enumerate the children of @p path.  Entries or -1. */
    int (*list)(tiku_trk_backend_t *b, const char *path,
                tiku_trk_entry_fn fn, void *ctx);

    /** @brief Read a node's value.  Bytes, or -1. */
    int (*read)(tiku_trk_backend_t *b, const char *path, void *buf,
                size_t max);

    /** @brief Write a node's value.  0, or -1 with @p err filled. */
    int (*write)(tiku_trk_backend_t *b, const char *path, const void *buf,
                 size_t len, char *err, size_t errmax);

    /** @brief Service the link and dispatch change notices.  Notices, or -1. */
    int (*pump)(tiku_trk_backend_t *b, int timeout_ms);

    /**
     * @brief Ask what changed since the last ask.
     *
     * One cheap question that answers "did anything happen at all", so a
     * view need not re-read a directory on a timer to find out.
     *
     * @param ids      Receives the identities that changed
     * @param max      Capacity of @p ids
     * @param complete Set to 0 when the answer is known to be partial, so
     *                 the caller re-reads instead of trusting it
     * @return Identities written, or -1 when the backend cannot tell (an
     *         older device, or a local filesystem), which means "poll".
     */
    int (*changes)(tiku_trk_backend_t *b, tiku_trk_change_t *out, int max,
                   int *complete);

    /**
     * @brief The path of the node carrying @p id, or -1.
     *
     * A change record names an identity; only the backend can say what that
     * identity is currently called.
     */
    int (*path_of_id)(tiku_trk_backend_t *b, uint64_t id, char *out,
                      size_t max);

    /**
     * @brief Change what an entry permits.  0, or -1 with @p err filled.
     *
     * NULL where the idea does not apply.  A device node's capability bits
     * are not the user's to set, and a backend that says so by leaving this
     * out is what lets the caller show the grid as read-only rather than
     * offering an edit that silently fails.
     */
    int (*setperm)(tiku_trk_backend_t *b, const char *path, unsigned perm,
                   char *err, size_t errmax);

    /** @brief The state store for entries of this backend. */
    struct tiku_trk_store *(*state_store)(tiku_trk_backend_t *b);

    void (*close)(tiku_trk_backend_t *b);
} tiku_trk_backend_ops_t;

/** @brief A backend instance; concrete ones extend this. */
struct tiku_trk_backend {
    const tiku_trk_backend_ops_t *ops;
    char                          devid[40];   /* empty for local files     */
    void                         *impl;
};

/** @brief Local files, with real attributes and permissions. */
tiku_trk_backend_t *tiku_trk_backend_posix_open(void);

/**
 * @brief A TikuOS device over the proven session/namespace client.
 *
 * @param port  Serial device, or NULL to take the first board found.
 * @param baud  Line rate.
 */
tiku_trk_backend_t *tiku_trk_backend_tiku_open(const char *port, int baud);

void tiku_trk_backend_close(tiku_trk_backend_t *b);

#endif /* TIKU_TRK_MODEL_H_ */
