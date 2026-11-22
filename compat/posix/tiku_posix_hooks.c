/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_hooks.c - the C library's file hooks and the directory calls,
 * answered by compat/posix, for a program with no file layer of its own.
 * Writes are not among them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "tiku_posix.h"

int _open(const char *path, int flags, int mode)
{
    (void)mode;
    return tiku_posix_open(path, flags);
}

_ssize_t _read(int fd, void *buf, size_t n)
{
    return tiku_posix_read(fd, buf, n);
}

_off_t _lseek(int fd, _off_t offset, int whence)
{
    return tiku_posix_lseek(fd, offset, whence);
}

int _close(int fd)
{
    return tiku_posix_close(fd);
}

int _fcntl(int fd, int cmd, int arg)
{
    (void)arg;
    return tiku_posix_fcntl(fd, cmd);
}

int _fstat(int fd, struct stat *st)
{
    return tiku_posix_fstat(fd, st);
}

int _stat(const char *path, struct stat *st)
{
    return tiku_posix_stat(path, st);
}

/** @brief No descriptor of this library is a terminal; a number it did not
 *         open is EBADF. */
int _isatty(int fd)
{
    errno = tiku_posix_is_open(fd) ? ENOTTY : EBADF;
    return 0;
}

DIR *opendir(const char *path)
{
    return tiku_posix_opendir(path);
}

struct dirent *readdir(DIR *dir)
{
    return tiku_posix_readdir(dir);
}

void rewinddir(DIR *dir)
{
    tiku_posix_rewinddir(dir);
}

int closedir(DIR *dir)
{
    return tiku_posix_closedir(dir);
}
