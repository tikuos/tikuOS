/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_state.h - where a node's UI state lives.
 *
 * Tracker's AttributeStreamNode seam, ported: every caller that reads or
 * writes pose info, column sets or view state goes through this one interface,
 * so "the object holds its own state" (local files, xattr) and "the viewer
 * holds it for the object" (device nodes, sidecar) are one code path.  The
 * fallback is Tracker's own -- it already relocates state for volumes that
 * cannot carry attributes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_STATE_H_
#define TIKU_TRK_STATE_H_

#include <stddef.h>
#include <stdint.h>

/* Attribute names, kept as Tracker spells them so the meaning is greppable
 * across both trees.  Local files really do carry these (prefixed for the
 * xattr namespace); the sidecar stores them under the same names. */
#define TIKU_TRK_ATTR_POSE_INFO   "_trk/pinfo"
#define TIKU_TRK_ATTR_COLUMNS     "_trk/columns"
#define TIKU_TRK_ATTR_VIEW_STATE  "_trk/viewstate"
/* The Desktop's and the Disks window's own slots (MA-064).  Separate
 * ATTRIBUTE names on real nodes rather than synthetic node keys, because
 * the xattr store can only write where a file actually is -- the same
 * reason the original redirects the root window's state to the Desktop
 * directory. */
#define TIKU_TRK_ATTR_DESK_VIEW_STATE  "_trk/desk_viewstate"
#define TIKU_TRK_ATTR_DISKS_VIEW_STATE "_trk/d_viewstate"
#define TIKU_TRK_ATTR_DISKS_COLUMNS    "_trk/d_columns"
/* Where a trashed item came from.  Tracker writes this on the node itself,
 * so it travels with the file; this store is keyed by path, so the record
 * is written under the item's TRASHED path and moves with it there. */
#define TIKU_TRK_ATTR_ORIGINAL    "_trk/original_path"
/* Where a folder's WINDOW was.  Kept per folder, beside its arrangement,
 * because a window belongs to the place it shows: one shared frame would
 * make every folder open where the last one was left. */
#define TIKU_TRK_ATTR_FRAME       "_trk/frame"
/* The set of windows that were open when the session ended, and the state
 * a folder falls back to when neither it nor its parent has one. */
#define TIKU_TRK_ATTR_SESSION     "_trk/session"
#define TIKU_TRK_ATTR_DEFAULTS    "_trk/window_default"
/* Where the Deskbar is docked and how wide it is.  Under the session slot
 * because the bar belongs to the desktop, not to any folder. */
#define TIKU_TRK_ATTR_DOCK        "_trk/dock"
#define TIKU_TRK_ATTR_DESKBAR_PREFS "_trk/deskbar_prefs"
/* The face dropped in the fonts folder that the interface is drawn in.
 * A NAME rather than a path: the folder is the list, and a family that
 * has been taken out of it should fall back rather than dangle. */
#define TIKU_TRK_ATTR_UI_FONT "_trk/ui_font"
#define TIKU_TRK_ATTR_DESKBAR_PREFS_FRAME "_trk/deskbar_prefs_frame"
#define TIKU_TRK_ATTR_REPLICANTS "_trk/replicants"

/* Reserved node keys reproducing Tracker's three-slot split: state for
 * things that cannot hold it goes to a well-known slot rather than to the
 * object (PoseView's FSGetDeskDir branch does exactly this for root and
 * Trash). */
#define TIKU_TRK_KEY_DEV_ROOT  "\x01root"
#define TIKU_TRK_KEY_DESKTOP   "\x01desk"
/* The session's own slot: the set of open windows belongs to no folder. */
#define TIKU_TRK_KEY_SESSION   "\x01session"

typedef struct tiku_trk_store tiku_trk_store_t;

/**
 * @brief Open the store for local files (extended attributes).
 *
 * State lives on the file itself, which is what makes a folder remember when
 * it is moved or copied to another host.
 */
tiku_trk_store_t *tiku_trk_store_xattr_open(void);

/**
 * @brief Open the per-device sidecar store.
 *
 * One file per device under $XDG_DATA_HOME/tracker/devices/<devid>.state, so
 * discarding a device touches exactly one file and cannot disturb where the
 * user put every other device.
 *
 * @param devid  From tiku_trk_ident_key().
 */
tiku_trk_store_t *tiku_trk_store_sidecar_open(const char *devid);

/** @brief Open a volatile store (tests, and nodes whose state must not persist). */
tiku_trk_store_t *tiku_trk_store_memory_open(void);

void tiku_trk_store_free(tiku_trk_store_t *s);

/**
 * @brief Read one attribute of one node.
 *
 * @param node  Absolute path, or a TIKU_TRK_KEY_* slot.
 * @return Bytes read, or -1 when absent.
 */
int tiku_trk_state_read(tiku_trk_store_t *s, const char *node,
                        const char *attr, void *buf, size_t max);

/** @brief Write one attribute.  @return 0, or -1 when the store refused. */
int tiku_trk_state_write(tiku_trk_store_t *s, const char *node,
                         const char *attr, const void *buf, size_t len);

/**
 * @brief Whether this store can persist at all.
 *
 * A read-only volume answers 0, and callers must degrade rather than fail:
 * Tracker keeps working on a CD, it just forgets.
 */
int tiku_trk_store_writable(const tiku_trk_store_t *s);

/**
 * @brief Persist pending changes.
 *
 * The sidecar batches: state is touched on directory switch, window close and
 * link loss, never once per drag.
 */
int tiku_trk_store_flush(tiku_trk_store_t *s);

/**
 * @brief Forget one node's state.
 *
 * ONLY for a user-invoked "forget arrangements this device no longer has".
 * INVARIANT: nothing on the pose-teardown path may call this -- a device
 * unplugged or a directory switched must leave its arrangement intact, or
 * reconnecting loses the user's desktop.
 */
int tiku_trk_state_forget(tiku_trk_store_t *s, const char *node);

/**
 * @brief Visit every attribute @p node carries.
 *
 * @param fn Called with the attribute name and its raw bytes; returning
 *           non-zero stops the walk.
 * @return how many were visited.
 */
int tiku_trk_state_each(tiku_trk_store_t *s, const char *node,
                        int (*fn)(const char *attr, const void *val,
                                  size_t len, void *ctx), void *ctx);

/** @brief Records held (tests and the store's own reporting). */
int tiku_trk_store_count(const tiku_trk_store_t *s);

/**
 * @brief Make the directory @p path lives in, and its parents.
 *
 * Everything this port writes into the user's home -- settings, the
 * thumbnail cache, the Deskbar's own folder -- assumed those directories
 * already existed, which is true of an account that has run other
 * software and false of a fresh one.  A write that fails because its
 * folder is missing looks exactly like a setting that does not persist.
 *
 * @return 0 when the directory exists afterwards.
 */
int tiku_trk_state_mkparents(const char *path);

/**
 * @brief Where this Tracker's Desktop is.
 *
 * Its OWN, under the user's configuration, rather than the desktop the
 * host system keeps: what shows here belongs to the device this is a
 * screen for, and is not a view of somebody's login session.
 *
 * @return @p out.
 */
const char *tiku_trk_desktop_dir(char *out, size_t max);

/**
 * @brief Where faces dropped in live: $TIKU_DESK_FONTS, else under the
 *        Tracker's own folder.  One answer, as for the Desktop.
 */
const char *tiku_trk_fonts_dir(char *out, size_t max);

#endif /* TIKU_TRK_STATE_H_ */
