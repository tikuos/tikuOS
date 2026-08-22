/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thumb.c - the thumbnail rules and the two queues.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_thumb.h"
#include "tiku_state.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * @brief Where a thumbnail's PIXELS live.
 *
 * Not on the file.  Tracker stores a WebP-compressed thumbnail as an
 * attribute; this port has no encoder, so the choice is 64 KB of raw pixels
 * per file in an attribute store built for small values, or a cache beside
 * the state store.  The cache wins: the attribute store's 4 KB cap is a
 * design decision about what attributes are FOR, and bulk data is not it.
 * The stamp and the dimensions stay attributes, so freshness is still a
 * property of the file.
 */
static int
blob_path(const char *path, char *out, size_t max)
{
    const char *home = getenv("HOME");
    unsigned long h = 5381ul;
    const unsigned char *p;

    if (home == NULL || home[0] == '\0') {
        return -1;
    }
    for (p = (const unsigned char *)path; *p != '\0'; p++) {
        h = ((h << 5) + h) ^ (unsigned long)*p;   /* djb2 */
    }
    /* No mkdir here: this is called by mere freshness CHECKS, and a
     * check must not conjure the cache the setting says not to build
     * (TS-053).  The save makes the directory when it writes. */
    return (snprintf(out, max, "%s/.cache/tiku-tracker/%08lx.raw", home,
                     h & 0xffffffffful) > 0) ? 0 : -1;
}

/** @brief Write @p n bytes to @p path.  @return 0 on success. */
static int
blob_write(const char *path, const void *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = (fwrite(buf, 1u, n, f) == n);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

void
tiku_thumb_init(tiku_thumb_q_t *q)
{
    if (q != NULL) {
        memset(q, 0, sizeof *q);
        q->enabled = 1;             /* the setting's default (TS-053) */
    }
}

int
tiku_thumb_wanted(const tiku_thumb_q_t *q, const char *path)
{
    if (q == NULL || !q->enabled) {
        return 0;
    }
    return tiku_image_is_image(path);
}

int
tiku_thumb_fresh(tiku_store_t *store, const char *path, int64_t mtime)
{
    int64_t made = 0;

    if (store == NULL || path == NULL) {
        return 0;
    }
    if (tiku_state_read(store, path, TIKU_ATTR_THUMB_TIME, &made,
                            sizeof made) != (int)sizeof made) {
        return 0;
    }
    /* STRICTLY newer.  A thumbnail stamped in the same second as an edit is
     * not evidence that the edit is in it. */
    return (made > mtime);
}

/** @brief Whether @p path is already queued or running. */
static int
in_flight(const tiku_thumb_q_t *q, const char *path)
{
    int i;

    if (strcmp(q->active, path) == 0) {
        return 1;
    }
    for (i = 0; i < q->nsmall; i++) {
        if (strcmp(q->small[i].path, path) == 0) {
            return 1;
        }
    }
    for (i = 0; i < q->nbig; i++) {
        if (strcmp(q->big[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_thumb_queue(tiku_thumb_q_t *q, const char *path, int64_t bytes,
                     int size)
{
    tiku_thumb_job_t *slot;

    if (q == NULL || !q->enabled || path == NULL || path[0] == '\0') {
        return -1;
    }
    /* The same file is never worked twice at once: two windows showing one
     * folder would otherwise both queue it (IV-033). */
    if (in_flight(q, path)) {
        return 0;
    }
    if (bytes >= TIKU_THUMB_BIG) {
        if (q->nbig >= TIKU_THUMB_QUEUE) {
            return -1;
        }
        slot = &q->big[q->nbig++];
    } else {
        if (q->nsmall >= TIKU_THUMB_QUEUE) {
            return -1;
        }
        slot = &q->small[q->nsmall++];
    }
    memset(slot, 0, sizeof *slot);
    snprintf(slot->path, sizeof slot->path, "%s", path);
    slot->bytes = bytes;
    slot->size = (size > 0) ? size : TIKU_THUMB_SIZE;
    return 1;
}

/** @brief Take the front job off @p list. */
static int
pop(tiku_thumb_job_t *list, int *n, tiku_thumb_job_t *out)
{
    int i;

    if (*n <= 0) {
        return 0;
    }
    *out = list[0];
    for (i = 1; i < *n; i++) {
        list[i - 1] = list[i];
    }
    (*n)--;
    return 1;
}

int
tiku_thumb_step(tiku_thumb_q_t *q, tiku_store_t *store)
{
    tiku_thumb_job_t job;
    tiku_image_t src, thumb;
    struct stat st;
    int64_t now_mtime = 0;

    if (q == NULL) {
        return 0;
    }
    /* Small files first: the whole reason for two queues is that a big one
     * must not stall them (IV-032). */
    if (!pop(q->small, &q->nsmall, &job) &&
        !pop(q->big, &q->nbig, &job)) {
        return 0;
    }
    snprintf(q->active, sizeof q->active, "%s", job.path);

    memset(&src, 0, sizeof src);
    memset(&thumb, 0, sizeof thumb);
    if (tiku_image_load(job.path, &src) != TIKU_IMG_OK) {
        q->active[0] = '\0';
        return 1;                   /* the job ran; it just found nothing */
    }
    if (tiku_image_thumb(&src, TIKU_THUMB_SIZE, &thumb) == 0) {
        if (store != NULL) {
            uint32_t dim[2];

            /* The picture first, then its stamp: a stamp written before the
             * data would make a half-written thumbnail look fresh. */
            char blob[TIKU_PATH_MAX];

            if (blob_path(job.path, blob, sizeof blob) == 0) {
                {   /* The write conjures the cache directory; nothing
                     * else does (TS-053). */
                    char dir[TIKU_PATH_MAX];
                    const char *home = getenv("HOME");

                    if (home != NULL &&
                        snprintf(dir, sizeof dir, "%s/.cache/tiku-tracker",
                                 home) > 0) {
                        (void)tiku_state_mkparents(dir);
                        (void)mkdir(dir, 0755);
                    }
                }
                (void)blob_write(blob, thumb.px,
                                 (size_t)thumb.w * (size_t)thumb.h *
                                     sizeof *thumb.px);
            }
            dim[0] = (uint32_t)src.w;
            dim[1] = (uint32_t)src.h;
            (void)tiku_state_write(store, job.path,
                                       TIKU_ATTR_THUMB_DIM, dim,
                                       sizeof dim);
            if (stat(job.path, &st) == 0) {
                now_mtime = (int64_t)st.st_mtime + 1;
            }
            (void)tiku_state_write(store, job.path,
                                       TIKU_ATTR_THUMB_TIME, &now_mtime,
                                       sizeof now_mtime);
        }
        q->made++;
        tiku_image_free(&thumb);
    }
    tiku_image_free(&src);
    q->active[0] = '\0';
    return 1;
}

int
tiku_thumb_load(tiku_store_t *store, const char *path,
                    tiku_image_t *out)
{
    size_t want = (size_t)TIKU_THUMB_SIZE * TIKU_THUMB_SIZE *
                  sizeof(uint32_t);

    if (store == NULL || path == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->px = malloc(want);
    if (out->px == NULL) {
        return -1;
    }
    {
        char blob[TIKU_PATH_MAX];
        FILE *f;
        size_t got = 0;

        if (blob_path(path, blob, sizeof blob) != 0 ||
            (f = fopen(blob, "rb")) == NULL) {
            tiku_image_free(out);
            return -1;
        }
        got = fread(out->px, 1u, want, f);
        (void)fclose(f);
        if (got != want) {
            tiku_image_free(out);
            return -1;
        }
    }
    out->w = TIKU_THUMB_SIZE;
    out->h = TIKU_THUMB_SIZE;
    return 0;
}
