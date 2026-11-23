/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_store.h - The calls between compat/posix's read group and its
 * store group, which only the library's own sources include.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_POSIX_STORE_H_
#define TIKU_POSIX_STORE_H_

#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/** @brief What the store group's open and stat return for a static node of
 *         the VFS under the store's directory: the read group answers it. */
#define TIKU_POSIX_STORE_FOREIGN (INT_MAX - 1)

/** @brief Non-zero when @p path is the store's directory or under it. */
int tiku_posix_store_path(const char *path);
/** @brief Non-zero when @p fd is in the store group's range. */
int tiku_posix_store_fd(int fd);

/* The read group's calls for the store's paths and descriptors; each sets
 * errno and returns -1 on failure, as the public call does. */
int tiku_posix_store_open(const char *path, int flags);
ssize_t tiku_posix_store_read(int fd, void *buf, size_t n);
off_t tiku_posix_store_lseek(int fd, off_t offset, int whence);
int tiku_posix_store_close(int fd);
int tiku_posix_store_fcntl(int fd, int cmd);
int tiku_posix_store_fstat(int fd, struct stat *st);
int tiku_posix_store_stat(const char *path, struct stat *st);
int tiku_posix_store_is_open(int fd);

/** @brief The node number the read group gives @p path, and readdir()'s
 *         d_ino: the same for a file of the store either way. */
uint32_t tiku_posix_node_number(const char *path);

#endif /* TIKU_POSIX_STORE_H_ */
