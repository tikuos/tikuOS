/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs.h - file operations.
 *
 * Copy, move, duplicate, rename, trash and undo, with Tracker's rules: the
 * same-volume test decides move versus copy, duplicate invents a name rather
 * than asking, and an operation reports progress so a window never freezes
 * waiting for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FS_H_
#define TIKU_FS_H_

#include <stddef.h>
#include <stdint.h>
#include "tiku_model.h"

struct tiku_volumes;

/** @brief What an operation is doing, for the progress window. */
typedef enum {
    TIKU_OP_IDLE = 0,
    TIKU_OP_COPY,
    TIKU_OP_MOVE,
    TIKU_OP_DUPLICATE,
    TIKU_OP_TRASH,
    TIKU_OP_DELETE,
    TIKU_OP_RENAME,
    TIKU_OP_LINK,
    TIKU_OP_NEW_FOLDER,
} tiku_op_t;

/** @brief How a name collision was resolved. */
typedef enum {
    TIKU_CONFLICT_ASK = 0,   /* stop and report; the UI decides       */
    TIKU_CONFLICT_REPLACE,
    TIKU_CONFLICT_SKIP,
    TIKU_CONFLICT_RENAME,    /* invent a fresh name, as Duplicate does */
    /* The two sticky answers.  "Replace" and "Skip" apply to the collision
     * in hand and the next one asks again; "all" is what makes a long job
     * finish without further questions (FS-019). */
    TIKU_CONFLICT_REPLACE_ALL,
    TIKU_CONFLICT_SKIP_ALL,
} tiku_conflict_t;

/** @brief Live state of a running operation. */
typedef struct {
    tiku_op_t op;
    int           total;          /* items to process                     */
    int           done;
    int           skipped;
    int           failed;
    char          current[TIKU_PATH_MAX];
    char          error[256];     /* the backend's own words, verbatim    */
    int           conflict;       /* 1 while stopped on a collision       */
    char          conflict_path[TIKU_PATH_MAX];
    /* What the question should OFFER.  A folder collision, or any
     * collision while several are pending, gets the four-way form; a lone
     * file-versus-file collision gets Cancel and Replace (FS-018, FS-019). */
    int           conflict_many;  /* four buttons rather than two          */
    int           conflict_isdir;
    int           collisions;     /* counted before the job starts         */
    /* Stopped on an error rather than a collision.  Separate from
     * `conflict` because the two questions are different questions: one
     * offers a name, the other offers to give up (FS-035). */
    int           error_ask;
    int           error_can_continue;  /* a delete failure cannot         */
    int           cancelled;      /* the job was stopped part-way          */
    /* Bytes actually written so far.  The counter the speed is computed
     * from: items done says nothing about rate when their sizes differ by
     * orders of magnitude. */
    int64_t       bytes;
    /* Where the job is putting things -- the "To: <dir>" half of what is
     * happening (FS-069).  Empty for jobs with no destination. */
    char          to[TIKU_PATH_MAX];
} tiku_progress_t;

/**
 * @brief Asked before every chunk; non-zero abandons the job.
 *
 * Per chunk rather than per item, because the thing being cancelled is
 * usually one large file and waiting for it to finish is the wait the
 * user just tried to escape.
 */
typedef int (*tiku_cancel_fn)(void *ctx);

typedef struct tiku_fs tiku_fs_t;

/**
 * @brief Told whenever an item lands under a new path (FS-060).
 *
 * Every move, rename, trashing, restore and undo of one goes through here,
 * which is where both paths are known at once: the shell that started the
 * job knows what it asked for, not what a collision renamed on the way.
 */
typedef void (*tiku_fs_moved_fn)(void *ctx, const char *from,
                                     const char *to);

/** @brief Listen for items landing somewhere else.  One listener. */
void tiku_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn,
                           void *ctx);

/** @brief Create the operation engine for one backend. */
tiku_fs_t *tiku_fs_new(tiku_backend_t *backend);

void tiku_fs_free(tiku_fs_t *fs);

/**
 * @brief Decide what a drop does: move within a volume, copy across one.
 *
 * Tracker's rule, and the reason dragging to another disk keeps the original.
 * @param force_copy  Non-zero when the user asked for a copy explicitly.
 * @return TIKU_OP_MOVE or TIKU_OP_COPY.
 */
tiku_op_t tiku_fs_drop_action(tiku_fs_t *fs, const char *src,
                                      const char *dst_dir, int force_copy);

/**
 * @brief The name Duplicate would give a copy of @p path.
 *
 * "<name> copy", then "copy 2", "copy 3"; duplicating something already
 * called "<name> copy" gives "copy 2", never "copy copy" (FS-006).
 */
int tiku_fs_duplicate_name(tiku_fs_t *fs, const char *dir,
                               const char *name, char *out, size_t max);

/**
 * @brief The name Create Link would use: the plain name unless it is taken,
 *        then "<name> link", "<name> link 2" (FS-009).
 */
int tiku_fs_link_name(tiku_fs_t *fs, const char *dir,
                          const char *name, char *out, size_t max);

/** @brief Start copying @p paths into @p dst_dir.  @return 0 if accepted. */
int tiku_fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_conflict_t policy);

/** @brief Start moving.  Items already in @p dst_dir are skipped (FS-005). */
int tiku_fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_conflict_t policy);

/** @brief Duplicate in place; each item lands in its OWN parent (FS-008). */
int tiku_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n);

/**
 * @brief Move to Trash, or delete where there is no Trash.
 *
 * The Trash itself is silently dropped from the selection first, so it is
 * never possible to trash the Trash.
 */
int tiku_fs_trash(tiku_fs_t *fs, const char *const *paths, int n);

/**
 * @brief Why an operation on an item was refused (FS-023..FS-025).
 *
 * Refusals, not questions: none of these has an "anyway" button, because
 * every one of them would destroy something the user did not name.
 */
typedef enum {
    TIKU_REFUSE_NONE = 0,
    TIKU_REFUSE_INTO_SELF,   /* a folder into itself or its own child */
    TIKU_REFUSE_BY_CHILD,    /* replacing a folder with its own child */
    TIKU_REFUSE_KIND,        /* a file over a folder, or the reverse   */
    TIKU_REFUSE_TRASH,       /* the Trash cannot be moved or copied    */
    TIKU_REFUSE_VOLUME       /* a whole volume is not an item to move  */
} tiku_refusal_t;

/**
 * @brief Whether moving or copying @p src into @p dst_dir is allowed.
 *
 * @param msg Receives the sentence to show; the wording is the reason.
 * @return TIKU_REFUSE_NONE when the operation may proceed.
 */
tiku_refusal_t tiku_fs_check_move(tiku_fs_t *fs, const char *src,
                                          const char *dst_dir, char *msg,
                                          size_t max);

/**
 * @brief Delete @p paths outright, with no Trash and no undo.
 *
 * Separate from trashing on purpose: "never trash the Trash" and "empty the
 * Trash" are both true, and one queue builder cannot hold both rules.
 *
 * @return 0 if accepted.
 */
int tiku_fs_delete(tiku_fs_t *fs, const char *const *paths, int n);

/*---------------------------------------------------------------------------*/
/* The Trash (FS-038, FS-039, FS-040, FS-044, FS-046, FS-047, FS-048)        */
/*---------------------------------------------------------------------------*/

/** @brief Where trashed items go, relative to the store's root. */
#define TIKU_TRASH_NAME ".Trash"

/**
 * @brief The Trash folder for @p path, creating it if absent.
 *
 * Asking CREATES it, as Tracker's does: "does this store have a Trash" is
 * answered by making sure it has one, and the trash-or-delete decision
 * depends on that answer.
 *
 * @return 0 on success.
 */
/**
 * @brief Every Trash there is, one per mounted volume (PVN-027, FS-081).
 *
 * A Trash is per filesystem -- that is what keeps trashing a rename rather
 * than a copy -- so "the Trash" is a UNION and not a place.  Made on demand
 * rather than at startup: a volume mounted read-only cannot have one, and
 * making them all at boot would touch every disk on the machine to no
 * purpose.
 *
 * @return how many were written.
 */
int tiku_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                           int max);

/** @brief Create and mark every writable mounted volume's Trash at startup. */
int tiku_fs_prepare_trash(tiku_fs_t *fs);

/**
 * @brief The same over a volume table the caller supplies.
 *
 * Separated so the rules -- skip the read-only, list each place once --
 * can be asked of a table that is known, rather than only of whatever this
 * machine happens to have mounted.
 */
int tiku_fs_trash_dirs_of(tiku_fs_t *fs,
                              const struct tiku_volumes *vs,
                              char out[][TIKU_PATH_MAX], int max);

int tiku_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out,
                          size_t max);

/**
 * @brief Whether this store can hold a Trash at all (FS-044).
 *
 * A device namespace cannot: its nodes are not files and there is nowhere
 * to put them.  The caller must then offer deletion outright rather than
 * silently deleting under the name "trash".
 */
int tiku_fs_has_trash(const tiku_fs_t *fs);

/**
 * @brief Put a trashed item back where it came from (FS-040).
 *
 * Missing parent folders are recreated.  Something already occupying the
 * original name is NOT overwritten: putting one thing back is not
 * permission to destroy another.
 *
 * @param err Receives the reason on refusal.
 * @return Number restored.
 */
int tiku_fs_restore(tiku_fs_t *fs, const char *const *paths, int n,
                        char *err, size_t errmax);

/** @brief Where a trashed item came from, or 0 when nothing was recorded. */
int tiku_fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                              size_t max);

/** @brief Empty the Trash.  @return Number removed. */
int tiku_fs_empty_trash(tiku_fs_t *fs);

/**
 * @brief What the Delete key should ASK (FS-046, FS-047).
 *
 * Three-way -- Cancel, Move to Trash, Delete -- except where the question
 * has no meaning: inside the Trash deletion is the only thing left, a store
 * with no Trash cannot offer to move anything to one, and Shift says the
 * user has already decided.
 */
typedef enum {
    TIKU_DELETE_ASK = 0,     /* the three-way question                */
    TIKU_DELETE_STRAIGHT     /* no question: delete outright          */
} tiku_delete_q_t;

tiku_delete_q_t tiku_fs_delete_question(const tiku_fs_t *fs,
                                                const char *path,
                                                int shift_held);

/** @brief Rename one entry.  @return 0, or -1 with @p err filled. */
int tiku_fs_rename(tiku_fs_t *fs, const char *path,
                       const char *new_name, char *err, size_t errmax);

/**
 * @brief Whether a name is acceptable before anything is attempted.
 *
 * Empty, "." / "..", and names containing '/' are refused; the message is
 * what the UI shows.
 */
int tiku_fs_name_ok(const char *name, char *err, size_t errmax);

/**
 * @brief The characters a name may not contain, as one string (MA-046).
 *
 * The editor refuses these at the keystroke rather than at commit, so the
 * set has to come from whoever owns the naming rules -- a namespace path and
 * a file name do not forbid the same things.
 */
const char *tiku_fs_name_deny(void);

/** @brief Advance the current operation a little.  @return items processed. */
int tiku_fs_step(tiku_fs_t *fs, int budget);

/** @brief Snapshot of what is happening, for the progress window. */
const tiku_progress_t *tiku_fs_progress(const tiku_fs_t *fs);

/** @brief Answer a conflict the engine stopped on and continue. */
void tiku_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how);

/** @brief Abandon the running operation; what is done stays done. */
void tiku_fs_cancel(tiku_fs_t *fs);

/**
 * @brief Install the mid-copy cancel check.
 *
 * @p fn is polled between chunks; returning non-zero stops the job where
 * it stands.
 */
void tiku_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn,
                                  void *ctx);

/**
 * @brief Answer the "would you like to continue?" the error put up.
 *
 * @p keep_going non-zero counts the item as failed and moves on; zero
 * abandons the rest of the job.
 */
void tiku_fs_resolve_error(tiku_fs_t *fs, int keep_going);

int tiku_fs_busy(const tiku_fs_t *fs);

/*---------------------------------------------------------------------------*/
/* Undo                                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Bytes free on @p path's filesystem, 0 when it will not say. */
uint64_t tiku_fs_free_bytes(const char *path);

/**
 * @brief What @p paths would cost on a destination, blocks included.
 *
 * Recurses; a directory costs one block of its own and a file costs its
 * size plus a block, the block clamped to [1024, 8192].
 */
uint64_t tiku_fs_items_size(const char *const *paths, int n);

/**
 * @brief Whether @p total plus 4 KB of headroom still fits.
 *
 * @return 1 when the copy may start.  A @p free_bytes of 0 means the
 *         filesystem would not say, which is not a reason to refuse.
 */
int tiku_fs_fits(uint64_t total, uint64_t free_bytes);

/** @brief How much ceremony a move of @p path needs (FS-027). */
typedef enum {
    TIKU_CONFIRM_NONE = 0,
    TIKU_CONFIRM_ASK,        /* config or settings: a plain question */
    TIKU_CONFIRM_OVERRIDE    /* system or home: Shift must be held   */
} tiku_confirm_t;

/**
 * @brief Whether moving @p path should stop and ask first.
 *
 * The system directories are matched by CONTAINMENT and the home folder by
 * exact match, which is the original's own asymmetry: a file inside home
 * moves without a word.
 */
tiku_confirm_t tiku_fs_confirm_move(const char *path);

/** @brief Whether @p path is the Trash or something inside it (FS-014). */
int tiku_fs_in_trash(tiku_fs_t *fs, const char *path);

/**
 * @brief Create symbolic links to @p paths in @p dst.
 *
 * @param relative Non-zero asks for a link relative to @p dst; it degrades
 *                 to an absolute one across a device boundary, where a
 *                 relative path cannot be walked (FS-010).
 */
int tiku_fs_link(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst, int relative);

/**
 * @brief The name a new folder would get in @p dir, collisions avoided.
 *
 * "New folder", then "New folder 2" .. "New folder 9", then "New folder10".
 */
void tiku_fs_new_folder_name(tiku_fs_t *fs, const char *dir,
                                 char *out, size_t max);

/**
 * @brief Create a new folder in @p dir; its path lands in @p out.
 *
 * Undoable: undo removes it again, redo makes it afresh.
 *
 * @return 0 on success.
 */
int tiku_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out,
                           size_t max, char *err, size_t errmax);

/**
 * @brief Create a new item by copying a stored document template.
 *
 * The destination name starts with "New <template name>" and is made unique
 * without replacing an existing item.  Files, folders and symbolic links are
 * copied with the same tree/permission/time rules as ordinary Tracker copy.
 */
int tiku_fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                                  const char *dst_dir, char *out, size_t max,
                                  char *err, size_t errmax);

/**
 * @brief Whether the last operation can be undone, and its description.
 *
 * A move and a rename are reversible; a copy is undone by removing what it
 * created; a delete is NOT undoable and says so.
 */
int tiku_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max);

/** @brief Reverse the last operation.  @return items reversed, or -1. */
int tiku_fs_undo(tiku_fs_t *fs, char *err, size_t errmax);

/**
 * @brief Whether the last undo can be put back, and its description.
 *
 * Doing anything new empties the redo history: it described a world that
 * the new operation has just replaced.
 */
int tiku_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max);

/**
 * @brief The paths the LAST finished operation brought into being.
 *
 * What a duplicate or a paste made, read from the same records undo keeps
 * -- so a shell can select what an operation created once it shows up
 * (AW-033), without a second list to keep true.
 *
 * @return Count written.
 */
int tiku_fs_last_created(const tiku_fs_t *fs,
                             char out[][TIKU_PATH_MAX], int max);

/** @brief Replay the operation the last undo reversed. */
int tiku_fs_redo(tiku_fs_t *fs, char *err, size_t errmax);

/** @brief The menu word for @p op ("Copy", "Move to Trash", ...). */
const char *tiku_fs_op_name(tiku_op_t op);

#endif /* TIKU_FS_H_ */
