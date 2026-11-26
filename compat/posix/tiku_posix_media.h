/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_posix_media.h - file operations on a mounted TikuOS media volume.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_POSIX_MEDIA_H
#define TIKU_POSIX_MEDIA_H
#include "tiku_posix_media_io.h"
#include <sys/types.h>

/** @brief A blocking call to the volume's owner. The owner retires the
 * request before returning and sets io->result on both refusal and completion. */
typedef struct {
    void (*request)(void *context, tiku_posix_media_io_t *io);
    void *context;
} tiku_posix_media_t;

/** @brief Bind a mounted card to a worker client in kernel context.
 * Reserve eight-byte-aligned queue storage for the lifetime of the client.
 * permissions is the client's TIKU_MEDIA_READ, WRITE and ADMIN mask.
 * A worker advances one bounded kernel phase per call. Queue contention
 * returns EBUSY. A blocking file call from kernel context returns EAGAIN.
 */
int tiku_posix_media_connect(tiku_posix_media_t *volume, void *queue, size_t size,
                            uint32_t client, unsigned permissions);

#define TIKU_POSIX_MEDIA_FOLDER ".folder"

typedef struct {
    char name[TIKU_POSIX_MEDIA_NAME];
    uint64_t id, size, epoch, rank;
    int directory;
} tiku_posix_media_entry_t;

/** @brief A caller-owned cursor over the immediate children of a flat
 * volume directory. marker is the .folder object's id if encountered. */
typedef struct {
    char prefix[TIKU_POSIX_MEDIA_NAME], next[TIKU_POSIX_MEDIA_NAME];
    uint64_t epoch, marker;
    unsigned exclusive, ended, saw_directory;
} tiku_posix_media_dir_t;

/** @brief Start at a directory relative to the volume root, "" for root. */
int tiku_posix_media_opendir(tiku_posix_media_dir_t *dir, const char *path);
/** @brief Return one child, zero at end, or -1 with errno. On ESTALE,
 * discard the partial listing and reopen. Each stream has its own cursor. */
int tiku_posix_media_readdir(const tiku_posix_media_t *, tiku_posix_media_dir_t *,
                            tiku_posix_media_entry_t *entry);
/** @brief Find the first catalog object at or after name, or strictly after it when exclusive. */
int tiku_posix_media_seek(const tiku_posix_media_t *, const char *name, int exclusive,
                         tiku_posix_media_entry_t *entry);
/** @brief Find a named media object and its position in the current catalog. */
int tiku_posix_media_stat(const tiku_posix_media_t *, const char *name,
                         tiku_posix_media_entry_t *entry);
/** @brief Open an entry for reading. With only name set, look it up first.
 * Refresh a stale catalog position once. Return a reader token or zero on error. */
uint64_t tiku_posix_media_open(const tiku_posix_media_t *, tiku_posix_media_entry_t *entry);
ssize_t tiku_posix_media_pread(const tiku_posix_media_t *, uint64_t reader, void *buf,
                              size_t length, uint64_t offset);
int tiku_posix_media_close(const tiku_posix_media_t *, uint64_t reader);
/** @brief Publish immutable bytes against the expected file ID, zero for
 * creation. Return the published ID or zero on error. */
uint64_t tiku_posix_media_replace(const tiku_posix_media_t *, const char *name, uint64_t expected,
                                 const void *bytes, size_t length);
int tiku_posix_media_unlink(const tiku_posix_media_t *, const char *name, uint64_t id);
/** @brief Rename a file. A NULL target ID requires an unused destination.
 * Otherwise replace that exact ID, or create the destination when *target is zero. */
int tiku_posix_media_rename(const tiku_posix_media_t *, const char *from, uint64_t id,
                           const char *to, const uint64_t *target);
/** @brief Append to a new or resumed stream and sync or finalize it.
 * The supplied bytes stay immutable until return. A started stream's ID
 * remains available to discard an interrupted write. */
int tiku_posix_media_flush(const tiku_posix_media_t *, const char *name, uint64_t *id,
                          const void *bytes, size_t length, int final);
int tiku_posix_media_discard(const tiku_posix_media_t *, const char *name, uint64_t id);
int tiku_posix_media_space(const tiku_posix_media_t *, uint64_t *total, uint64_t *available);

/** @brief Journal keyed by directory inode. The containing namespace
 * serializes publication and reads. Each reader owns its sequence number. */
#define TIKU_POSIX_MEDIA_CHANGES 64u
typedef struct {
    uint64_t dirs[TIKU_POSIX_MEDIA_CHANGES], sequence;
} tiku_posix_media_changes_t;
void tiku_posix_media_changed(tiku_posix_media_changes_t *, uint64_t directory);
/** @brief Read up to max IDs. UINT64_MAX starts at the newest change.
 * complete is zero when the reader missed records older than the journal. */
int tiku_posix_media_changes(const tiku_posix_media_changes_t *, uint64_t *next,
                            uint64_t *dirs, int max, int *complete);
#endif
