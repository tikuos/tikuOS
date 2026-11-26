/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_posix_media.c - POSIX media readers, versioned writes and directory cursors.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_posix_media.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif

static int fail(int error)
{
    errno = error;
    return -1;
}

static int copy_name(char *dst, const char *src)
{
    if (!src) return fail(EINVAL);
    size_t n = strnlen(src, TIKU_POSIX_MEDIA_NAME);
    if (n == TIKU_POSIX_MEDIA_NAME) return fail(ENAMETOOLONG);
    memcpy(dst, src, n + 1u);
    return 0;
}

static int request(const tiku_posix_media_t *volume, tiku_posix_media_io_t *io)
{
    if (!volume || !volume->request) return fail(EINVAL);
    volume->request(volume->context, io);
    switch (io->result) {
    case TIKU_POSIX_MEDIA_OK: return 0;
    case TIKU_POSIX_MEDIA_END: return fail(ENOENT);
    case TIKU_POSIX_MEDIA_BUSY: return fail(EBUSY);
    case TIKU_POSIX_MEDIA_STALE: return fail(ESTALE);
    case TIKU_POSIX_MEDIA_INVALID: return fail(EINVAL);
    case TIKU_POSIX_MEDIA_UNSUPPORTED: return fail(ENOTSUP);
    case TIKU_POSIX_MEDIA_LIMIT: return fail(ENFILE);
    case TIKU_POSIX_MEDIA_FULL: return fail(ENOSPC);
    case TIKU_POSIX_MEDIA_AGAIN: return fail(EAGAIN);
    case TIKU_POSIX_MEDIA_DENIED: return fail(EACCES);
    default: return fail(EIO);
    }
}

int tiku_posix_media_opendir(tiku_posix_media_dir_t *dir, const char *path)
{
    if (!dir || !path) return fail(EINVAL);
    *dir = (tiku_posix_media_dir_t){0};
    size_t n = strnlen(path, sizeof dir->prefix);
    if (n == sizeof dir->prefix || (n + 1u == sizeof dir->prefix && path[n - 1] != '/'))
        return fail(ENAMETOOLONG);
    memcpy(dir->prefix, path, n);
    if (n && path[n - 1] != '/') dir->prefix[n++] = '/';
    memcpy(dir->next, dir->prefix, n + 1u);
    return 0;
}

static int entry_of(const tiku_posix_media_io_t *io, tiku_posix_media_entry_t *entry)
{
    if (!memchr(io->found, 0, sizeof io->found)) return fail(EIO);
    memcpy(entry->name, io->found, sizeof entry->name);
    entry->id = io->id;
    entry->size = io->size;
    entry->rank = io->rank;
    entry->epoch = io->epoch;
    entry->directory = io->directory;
    return 0;
}

/* A native directory sorts before its descendants. Names with punctuation
 * can sort between them, so looking up the parent preserves those siblings. */
static int listed_directory(const tiku_posix_media_t *v, const char *name, uint64_t epoch)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_ENUMERATE, .epoch = epoch};
    memcpy(io.name, name, strlen(name) + 1u);
    if (request(v, &io) < 0) return -1;
    return io.directory && !strncmp(io.found, name, sizeof io.found);
}

int tiku_posix_media_readdir(const tiku_posix_media_t *v, tiku_posix_media_dir_t *d,
                            tiku_posix_media_entry_t *entry)
{
    if (!d || !entry) return fail(EINVAL);
    size_t prefix = strlen(d->prefix);
    while (!d->ended) {
        tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_ENUMERATE,
                                    .exclusive = d->exclusive, .epoch = d->epoch};
        memcpy(io.name, d->next, sizeof io.name);
        int saved = errno;
        if (request(v, &io) < 0) {
            if (io.result != TIKU_POSIX_MEDIA_END) return -1;
            errno = saved;
            d->ended = 1;
            break;
        }
        if (entry_of(&io, entry) < 0) return -1;
        if (strncmp(entry->name, d->prefix, prefix)) {
            d->ended = 1;
            break;
        }
        d->epoch = io.epoch;
        char *leaf = entry->name + prefix;
        char *slash = strchr(leaf, '/');
        if (slash) {
            if (slash == leaf) return fail(EIO);
            size_t n = (size_t)(slash - entry->name);
            *slash = 0;
            int listed = d->saw_directory ? listed_directory(v, entry->name, d->epoch) : 0;
            if (listed < 0) return -1;
            memcpy(d->next, entry->name, n);
            d->next[n] = '0';
            d->next[n + 1] = 0;
            d->exclusive = 0;
            if (listed) continue;
            entry->directory = 1;
            entry->id = entry->size = 0;
        } else {
            memcpy(d->next, entry->name, sizeof d->next);
            d->exclusive = 1;
            if (entry->directory) d->saw_directory = 1;
            else if (!strcmp(leaf, TIKU_POSIX_MEDIA_FOLDER)) {
                d->marker = io.id;
                continue;
            }
        }
        if (*leaf) return 1;
    }
    return 0;
}

int tiku_posix_media_seek(const tiku_posix_media_t *v, const char *name, int exclusive,
                         tiku_posix_media_entry_t *entry)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_ENUMERATE, .exclusive = !!exclusive};
    if (!entry) return fail(EINVAL);
    if (copy_name(io.name, name) < 0 || request(v, &io) < 0) return -1;
    return entry_of(&io, entry);
}

int tiku_posix_media_stat(const tiku_posix_media_t *v, const char *name, tiku_posix_media_entry_t *entry)
{
    tiku_posix_media_entry_t found;
    if (!entry) return fail(EINVAL);
    if (tiku_posix_media_seek(v, name, 0, &found) < 0) return -1;
    if (strcmp(name, found.name)) return fail(ENOENT);
    *entry = found;
    return 0;
}

uint64_t tiku_posix_media_open(const tiku_posix_media_t *v, tiku_posix_media_entry_t *entry)
{
    if (!entry) { fail(EINVAL); return 0; }
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        if (!entry->epoch && tiku_posix_media_stat(v, entry->name, entry) < 0) return 0;
        tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_OPEN,
                                    .epoch = entry->epoch, .rank = entry->rank};
        if (copy_name(io.name, entry->name) < 0) return 0;
        if (request(v, &io) == 0) return io.reader;
        if (errno != ESTALE) return 0;
        entry->epoch = 0;
    }
    return 0;
}

ssize_t tiku_posix_media_pread(const tiku_posix_media_t *v, uint64_t reader, void *buf,
                              size_t length, uint64_t offset)
{
    size_t done = 0;
    if (!reader || (!buf && length)) return fail(EINVAL);
    if (length > (size_t)SSIZE_MAX || length > UINT64_MAX - offset) return fail(EOVERFLOW);
    while (done < length) {
        size_t want = length - done;
        if (want > TIKU_POSIX_MEDIA_CHUNK) want = TIKU_POSIX_MEDIA_CHUNK;
        tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_READ, .reader = reader,
            .offset = offset + done, .bytes = (unsigned char *)buf + done, .length = want};
        if (request(v, &io) < 0) return done ? (ssize_t)done : -1;
        if (io.count > want) return done ? (ssize_t)done : fail(EIO);
        done += io.count;
        if (io.count < want) break;
    }
    return (ssize_t)done;
}

int tiku_posix_media_close(const tiku_posix_media_t *v, uint64_t reader)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_CLOSE, .reader = reader};
    return request(v, &io);
}

uint64_t tiku_posix_media_replace(const tiku_posix_media_t *v, const char *name, uint64_t expected,
                                 const void *bytes, size_t length)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_REPLACE, .id = expected,
                                .bytes = (unsigned char *)bytes, .length = length};
    if (length > TIKU_POSIX_MEDIA_CHUNK) { fail(EFBIG); return 0; }
    if (length && !bytes) { fail(EINVAL); return 0; }
    if (copy_name(io.name, name) < 0 || request(v, &io) < 0) return 0;
    return io.id;
}

int tiku_posix_media_unlink(const tiku_posix_media_t *v, const char *name, uint64_t id)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_DELETE, .id = id};
    return copy_name(io.name, name) < 0 ? -1 : request(v, &io);
}

int tiku_posix_media_rename(const tiku_posix_media_t *v, const char *from, uint64_t id,
                           const char *to, const uint64_t *target)
{
    tiku_posix_media_io_t io = {.operation = target ? TIKU_POSIX_MEDIA_RENAME_OVER : TIKU_POSIX_MEDIA_RENAME,
                                .id = id, .offset = target ? *target : 0};
    if (copy_name(io.name, from) < 0 || copy_name(io.destination, to) < 0) return -1;
    return request(v, &io);
}

int tiku_posix_media_flush(const tiku_posix_media_t *v, const char *name, uint64_t *id,
                          const void *bytes, size_t length, int final)
{
    if (!id || (length && !bytes)) return fail(EINVAL);
    if (length > TIKU_POSIX_MEDIA_CHUNK) return fail(EFBIG);
    tiku_posix_media_io_t io = {.operation = *id ? TIKU_POSIX_MEDIA_RESUME : TIKU_POSIX_MEDIA_BEGIN,
                                .id = *id};
    if (copy_name(io.name, name) < 0 || request(v, &io) < 0) return -1;
    *id = io.id;
    int result = 0;
    if (length) {
        io.operation = TIKU_POSIX_MEDIA_APPEND;
        io.bytes = (unsigned char *)bytes;
        io.length = length;
        result = request(v, &io);
    }
    io.bytes = NULL;
    io.length = 0;
    if (result == 0) {
        io.operation = final ? TIKU_POSIX_MEDIA_FINALIZE : TIKU_POSIX_MEDIA_SYNC;
        result = request(v, &io);
        if (result == 0 && final) return 0;
    }
    int saved = errno;
    io.operation = TIKU_POSIX_MEDIA_RELEASE;
    int release = request(v, &io);
    if (result == 0) return release;
    return fail(saved);
}

int tiku_posix_media_discard(const tiku_posix_media_t *v, const char *name, uint64_t id)
{
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_DISCARD, .id = id};
    if (copy_name(io.name, name) < 0) return -1;
    if (request(v, &io) == 0) return 0;
    if (errno != ESTALE) return -1;
    return tiku_posix_media_unlink(v, io.name, id);
}

int tiku_posix_media_space(const tiku_posix_media_t *v, uint64_t *total, uint64_t *available)
{
    if (!total || !available) return fail(EINVAL);
    tiku_posix_media_io_t io = {.operation = TIKU_POSIX_MEDIA_SPACE};
    if (request(v, &io) < 0) return -1;
    *total = io.offset;
    *available = io.size;
    return 0;
}

void tiku_posix_media_changed(tiku_posix_media_changes_t *changes, uint64_t directory)
{
    changes->dirs[changes->sequence++ % TIKU_POSIX_MEDIA_CHANGES] = directory;
}

int tiku_posix_media_changes(const tiku_posix_media_changes_t *changes, uint64_t *next,
                            uint64_t *dirs, int max, int *complete)
{
    if (!changes || !next || !complete || max < 0 || (max && !dirs)) return fail(EINVAL);
    *complete = 1;
    if (*next == UINT64_MAX) *next = changes->sequence;
    else if (*next > changes->sequence || changes->sequence - *next > TIKU_POSIX_MEDIA_CHANGES) {
        *complete = 0;
        *next = changes->sequence > TIKU_POSIX_MEDIA_CHANGES ? changes->sequence - TIKU_POSIX_MEDIA_CHANGES : 0;
    }
    int count = 0;
    while (*next < changes->sequence && count < max)
        dirs[count++] = changes->dirs[(*next)++ % TIKU_POSIX_MEDIA_CHANGES];
    return count;
}
