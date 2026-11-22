/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix.h - compat/posix's file calls by their own names, for a program
 * whose own file layer keeps the C library's hooks and hands it kernel paths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_POSIX_H_
#define TIKU_POSIX_H_

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/** @brief The first descriptor the library returns; 0 to 2 are the C
 *         library's standard streams. */
#define TIKU_POSIX_FD_BASE 3

/** @brief Descriptors open at once, over every caller. */
#ifndef TIKU_POSIX_FDS
#define TIKU_POSIX_FDS 8
#endif

/** @brief Directory streams open at once, over every caller. */
#ifndef TIKU_POSIX_DIRS
#define TIKU_POSIX_DIRS 4
#endif

/** @brief Bytes one read returns at most; a node the kernel renders whole
 *         gives its first TIKU_POSIX_READ_MAX - 1 bytes. */
#ifndef TIKU_POSIX_READ_MAX
#define TIKU_POSIX_READ_MAX 256
#endif

/*
 * Each call below runs its kernel work on the kernel thread: from a worker
 * through tiku_thread_call(), from the kernel thread at once.  None of them
 * waits.  Paths are absolute kernel VFS paths under TIKU_VFS_PATH_MAX bytes;
 * every node is read-only.  Failures return -1 (NULL for the directory
 * calls) with errno set; compat/posix/AGENTS.md lists each result.  This
 * header names only C library types, so a file layer built without the
 * kernel's include paths can include it.
 */

/** @brief Open @p path for reading: a readable file, or a directory. */
int tiku_posix_open(const char *path, int flags);
/** @brief Read up to @p n bytes at the descriptor's offset and advance it. */
ssize_t tiku_posix_read(int fd, void *buf, size_t n);
/** @brief Move a file descriptor's offset: SEEK_SET or SEEK_CUR. */
off_t tiku_posix_lseek(int fd, off_t offset, int whence);
/** @brief Close a descriptor; its number may be returned by a later open. */
int tiku_posix_close(int fd);
/** @brief F_GETFL (O_RDONLY), F_GETFD and F_SETFD (no flags kept). */
int tiku_posix_fcntl(int fd, int cmd);
/** @brief An open descriptor's node: its mode, and st_ino a number derived
 *         from its path, which readdir()'s d_ino gives too.  The size and
 *         st_dev are 0. */
int tiku_posix_fstat(int fd, struct stat *st);
/** @brief @p path's node, as tiku_posix_fstat() describes it. */
int tiku_posix_stat(const char *path, struct stat *st);
/** @brief Non-zero when @p fd is open in this library. */
int tiku_posix_is_open(int fd);
/** @brief Open a directory stream over @p path's children. */
DIR *tiku_posix_opendir(const char *path);
/** @brief The next child, NULL at the end (errno unchanged) or on a fault. */
struct dirent *tiku_posix_readdir(DIR *dir);
/** @brief Start a directory stream over from its first child. */
void tiku_posix_rewinddir(DIR *dir);
/** @brief Close a directory stream. */
int tiku_posix_closedir(DIR *dir);
/** @brief Non-zero when @p dir is a stream of this library. */
int tiku_posix_is_dir_stream(const DIR *dir);

/**
 * @brief The VFS caller capability (TIKU_VFS_CAP_* bits) the library's calls
 *        run under, set for each call and the caller's restored on every
 *        return.
 * @return The previous one.  TIKU_VFS_CAP_NONE (0) until set.
 */
uint8_t tiku_posix_authority_set(uint8_t cap);

#endif /* TIKU_POSIX_H_ */
