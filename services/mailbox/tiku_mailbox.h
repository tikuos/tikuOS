/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mailbox.h - files two processors share on one serial FRAM.
 *
 * Named files on a part both processors reach through a switched bus.  A
 * file the other processor has read to its end may be reclaimed for room,
 * unless it is under keep/.  A cut at any byte leaves every file whole.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MAILBOX_H_
#define TIKU_MAILBOX_H_

#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* LIMITS                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Files the table holds. */
#define TIKU_MAILBOX_FILES     255u
/** @brief Longest name, NUL included: 23 characters, keep/ counted. */
#define TIKU_MAILBOX_NAME_MAX  24u
/** @brief The folder whose files are never reclaimed. */
#define TIKU_MAILBOX_KEEP      "keep/"
/** @brief Bytes of a data block, the unit space is given in. */
#define TIKU_MAILBOX_BLOCK     64u

/*---------------------------------------------------------------------------*/
/* THE MEDIUM                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief The part and its bus, as the board reaches them.
 *
 * read and write return 0 or a negative status.  take gets the bus for one
 * operation and returns 0 or a negative status; give hands it back.  side
 * names this processor, 1 to 255, different on each processor.
 */
typedef struct {
    int    (*read)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
    int    (*write)(void *ctx, uint32_t addr, const uint8_t *buf,
                    uint32_t len);
    int    (*take)(void *ctx);
    void   (*give)(void *ctx);
    void    *ctx;
    uint32_t size;              /**< Bytes of the part */
    uint8_t  side;              /**< This processor */
} tiku_mailbox_medium_t;

/** @brief What tiku_mailbox_check() found. */
typedef struct {
    uint16_t files;             /**< Files in the table */
    uint16_t kept;              /**< Of them, under keep/ */
    uint16_t read;              /**< Of the others, read by the other side:
                                     reclaimable */
    uint16_t bad;               /**< Files whose data fails its CRC */
    uint32_t used;              /**< Bytes of the files' data */
    uint32_t free;              /**< Bytes no file holds */
    uint32_t largest;           /**< Longest file that fits now */
    uint32_t reclaimed;         /**< Blocks freed that no file held */
} tiku_mailbox_stats_t;

/** @brief Called by tiku_mailbox_list() for each file. */
typedef void (*tiku_mailbox_list_fn)(const char *name, uint32_t len,
                                     void *ctx);

/*---------------------------------------------------------------------------*/
/* CALLS                                                                     */
/*---------------------------------------------------------------------------*/
/*
 * A name is 1 to 23 printable characters without a space; it may start
 * with keep/ and has no other '/'.  Every call returns 0 or a byte count,
 * or a negative TIKU_VFS_* status: ENOTSUP when the part holds no mailbox,
 * ENOENT for no such file, EINVAL for a bad name, E2BIG when the data does
 * not fit beside the files that are kept or unread, EBUSY when the bus was
 * not had and EIO for a medium error.
 */

/** @brief Write an empty mailbox over the whole part. */
int tiku_mailbox_format(const tiku_mailbox_medium_t *m);

/**
 * @brief Create or replace a file.  The old content stays readable until
 *        the new content is whole; the new content is unread.  Short of
 *        room, the oldest read file outside keep/ is reclaimed first.
 */
int tiku_mailbox_write(const tiku_mailbox_medium_t *m, const char *name,
                       const uint8_t *data, uint32_t len);

/**
 * @brief Copy at most @p max bytes of a file from @p offset on.  A read by
 *        the other processor that reaches the end marks the file read.
 * @param total  Out, optional: the file's length
 * @return the bytes copied, 0 at or past the end
 */
int tiku_mailbox_read_at(const tiku_mailbox_medium_t *m, const char *name,
                         uint32_t offset, uint8_t *buf, uint32_t max,
                         uint32_t *total);

/** @brief The length of a file, read or not: it marks nothing. */
int tiku_mailbox_stat(const tiku_mailbox_medium_t *m, const char *name,
                      uint32_t *len);

/** @brief Delete a file. */
int tiku_mailbox_delete(const tiku_mailbox_medium_t *m, const char *name);

/** @brief Call @p fn for each file, in table order. */
int tiku_mailbox_list(const tiku_mailbox_medium_t *m,
                      tiku_mailbox_list_fn fn, void *ctx);

/**
 * @brief Check every file's data against its CRC and give back the space
 *        no file holds, which an operation cut short leaves behind.
 */
int tiku_mailbox_check(const tiku_mailbox_medium_t *m,
                       tiku_mailbox_stats_t *out);

#endif /* TIKU_MAILBOX_H_ */
