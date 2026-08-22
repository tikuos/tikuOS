/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thumb.h - thumbnails: when to make one, where it lives, and the
 * queue that makes it without stopping the window.
 *
 * The rules here are the ones the rows are about: a cached thumbnail is
 * trusted only while it is newer than the file, a big image must not stall
 * the small ones behind it, the same file is never worked twice at once, and
 * a pose shows a type icon until the picture is ready rather than waiting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_THUMB_H_
#define TIKU_THUMB_H_

#include "tiku_image.h"
#include "tiku_model.h"
#include "tiku_state.h"



#include <stdint.h>

/** @brief The edge of a stored thumbnail, in pixels (Tracker's B_XXL_ICON). */
#define TIKU_THUMB_SIZE 128

/**
 * @brief Above this size an image goes on the slow queue.
 *
 * Tracker's 128 KB.  The split exists so one enormous file cannot hold up
 * the dozen small ones behind it -- the window fills in the order the eye
 * would want, not the order the directory happened to list.
 */
#define TIKU_THUMB_BIG 131072

/** @brief How many jobs may be waiting on each queue. */
#define TIKU_THUMB_QUEUE 32

/** @brief Where a thumbnail and its stamp live on the file. */
#define TIKU_ATTR_THUMB      "_trk/thumb"
#define TIKU_ATTR_THUMB_TIME "_trk/thumbtime"
#define TIKU_ATTR_THUMB_DIM  "_trk/thumbdim"

/** @brief One queued piece of work. */
typedef struct {
    char    path[TIKU_PATH_MAX];
    int     size;                   /* the icon size asked for          */
    int64_t bytes;                  /* the file's size, which picks the
                                     * queue                            */
} tiku_thumb_job_t;

/** @brief The two queues and what is in flight. */
typedef struct {
    tiku_thumb_job_t small[TIKU_THUMB_QUEUE];
    int                  nsmall;
    tiku_thumb_job_t big[TIKU_THUMB_QUEUE];
    int                  nbig;
    char                 active[TIKU_PATH_MAX];  /* "" when idle    */
    int                  made;      /* thumbnails produced this session */
    int                  enabled;   /* the "Generate image thumbnails"
                                     * setting; on by default           */
} tiku_thumb_q_t;

void tiku_thumb_init(tiku_thumb_q_t *q);

/**
 * @brief Whether @p path should get a thumbnail at all.
 *
 * The setting first, then the kind: only an image earns one.
 */
int tiku_thumb_wanted(const tiku_thumb_q_t *q, const char *path);

/**
 * @brief Whether the stored thumbnail may still be trusted.
 *
 * Strictly newer than the file: a thumbnail made in the same second as an
 * edit is not evidence the edit is in it.
 */
int tiku_thumb_fresh(tiku_store_t *store, const char *path,
                         int64_t mtime);

/**
 * @brief Queue @p path.
 *
 * @return 1 when it was queued, 0 when it was already in flight or queued
 *         (the same file is never worked twice at once), -1 when the queue
 *         is full.
 */
int tiku_thumb_queue(tiku_thumb_q_t *q, const char *path,
                         int64_t bytes, int size);

/**
 * @brief Do at most one queued job.
 *
 * Small files first, so a big one cannot stall them.  Called from the
 * shell's idle tick, which is what keeps the window answering while the
 * picture is made.
 *
 * @return 1 when a job ran.
 */
int tiku_thumb_step(tiku_thumb_q_t *q, tiku_store_t *store);

/**
 * @brief Read a stored thumbnail back into @p out.
 *
 * @return 0 on success.
 */
int tiku_thumb_load(tiku_store_t *store, const char *path,
                        tiku_image_t *out);

#endif /* TIKU_THUMB_H_ */
