/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_posix_media_io.h - queued operations on a mounted media volume.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_POSIX_MEDIA_IO_H
#define TIKU_POSIX_MEDIA_IO_H
#include <stddef.h>
#include <stdint.h>
#define TIKU_POSIX_MEDIA_NAME 64u
#define TIKU_POSIX_MEDIA_CHUNK 65536u

/** @brief Caller reservation for the queued read and mutation bridge; aligned to eight bytes. */
#define TIKU_POSIX_MEDIA_BRIDGE_BYTES 16384u
/** @brief Retained state slots for the firmware card owners. */
#define TIKU_POSIX_MEDIA_READ_BYTES 6144u
#define TIKU_POSIX_MEDIA_MUTATION_BYTES 8192u
/** @brief Operations advance through the firmware's serialized storage queue.
 *  SPACE answers at once: offset is the volume's bytes, size a writer's.
 *  BEGIN starts stream name (its id comes back), RESUME takes stream id up,
 *  APPEND adds up to 64 KiB, SYNC commits, FINALIZE completes, RELEASE gives
 *  the writer up; DISCARD removes unfinished object id named name; RENAME_OVER
 *  moves id from name to destination over visible file offset (0: none). */
enum {
    TIKU_POSIX_MEDIA_ENUMERATE,
    TIKU_POSIX_MEDIA_OPEN,
    TIKU_POSIX_MEDIA_READ,
    TIKU_POSIX_MEDIA_CLOSE,
    TIKU_POSIX_MEDIA_RENAME,
    TIKU_POSIX_MEDIA_DELETE,
    TIKU_POSIX_MEDIA_REPLACE,
    TIKU_POSIX_MEDIA_MKDIR,
    TIKU_POSIX_MEDIA_TREE_RENAME,
    TIKU_POSIX_MEDIA_TREE_DELETE,
    TIKU_POSIX_MEDIA_DIRECTORY_UPGRADE,
    TIKU_POSIX_MEDIA_TREE_COPY,
    TIKU_POSIX_MEDIA_SPACE,
    TIKU_POSIX_MEDIA_BEGIN,
    TIKU_POSIX_MEDIA_RESUME,
    TIKU_POSIX_MEDIA_APPEND,
    TIKU_POSIX_MEDIA_SYNC,
    TIKU_POSIX_MEDIA_FINALIZE,
    TIKU_POSIX_MEDIA_RELEASE,
    TIKU_POSIX_MEDIA_DISCARD,
    TIKU_POSIX_MEDIA_RENAME_OVER
};
/** @brief Queue and completion results. FULL means the volume has no room for the change. */
enum {
    TIKU_POSIX_MEDIA_OK,
    TIKU_POSIX_MEDIA_END,
    TIKU_POSIX_MEDIA_BUSY,
    TIKU_POSIX_MEDIA_STALE,
    TIKU_POSIX_MEDIA_UNAVAILABLE,
    TIKU_POSIX_MEDIA_INVALID,
    TIKU_POSIX_MEDIA_UNSUPPORTED,
    TIKU_POSIX_MEDIA_LIMIT,
    TIKU_POSIX_MEDIA_FULL,
    TIKU_POSIX_MEDIA_DENIED,
    TIKU_POSIX_MEDIA_AGAIN
};
/** @brief Zero-initialize a request and NUL-terminate its names. Keep the record and buffers until poll reports completion. */
typedef struct {
    unsigned operation, exclusive;
    unsigned directory;
    uint64_t prepared, total;
    char name[TIKU_POSIX_MEDIA_NAME], found[TIKU_POSIX_MEDIA_NAME], destination[TIKU_POSIX_MEDIA_NAME];
    uint64_t epoch, rank, reader, offset, size, id;
    unsigned char *bytes;
    size_t length, count;
    int result;
} tiku_posix_media_io_t;
/** @brief Bytes of aligned storage required by one card client, or SIZE_MAX if the card owners exceed the reservation. */
size_t tiku_posix_media_queue_size(void);
/** @brief Initialize an eight-byte-aligned queue in owner context.
 * @param client Nonzero media service identity, distinct for each client.
 * @param permissions Allowed TIKU_MEDIA_READ, WRITE and ADMIN bits.
 * Submit, poll, idle and cancel run in serialized kernel context.
 */
void tiku_posix_media_queue_init(void *storage, uint32_t client, unsigned permissions);
/** @brief Enqueue one operation; BUSY retains nothing and permits retry. */
int tiku_posix_media_queue_submit(void *, tiku_posix_media_io_t *);
/** @brief Advance one bounded shared storage phase; nonzero retires the submitted view. */
int tiku_posix_media_queue_poll(void *);
/** @brief Request cancellation and drain active card work. Poll a submitted service request to retire its record. */
int tiku_posix_media_queue_cancel(void *);
/** @brief On an owner turn with no request: close a card session left
 *  parked longer than the build's linger; 0, or TIKU_MFS_BUSY or
 *  TIKU_MFS_IO from closing it. */
int tiku_posix_media_queue_idle(void *);
/** @brief Changes to the card's names this bridge did not make, counted at
 *  its submissions, completions and idle turns since it was initialized;
 *  read from any thread. */
unsigned tiku_posix_media_queue_foreign(const void *);
#endif
