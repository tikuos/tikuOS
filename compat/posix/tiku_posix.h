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

/*---------------------------------------------------------------------------*/
/* THE STORE GROUP                                                           */
/*---------------------------------------------------------------------------*/

/*
 * Files under TIKU_POSIX_STORE_PREFIX are files of the kernel's file store,
 * named by the rest of the path.  A descriptor that writes keeps the file's
 * bytes in a private stage; fsync() and a close after writes publish the
 * stage as the file's new content, and rename() gives a file another name,
 * replacing one; each returns once its change is durable.  The calls that
 * publish wait, so on the kernel thread they return EAGAIN and change
 * nothing.  With the group linked (TIKU_POSIX_STORE) the read group's calls
 * hand these paths and descriptors to it.
 */

/** @brief The path prefix of the store's files. */
#define TIKU_POSIX_STORE_PREFIX "/data/"
/** @brief Descriptors open on the store at once, numbered after the read
 *         group's. */
#ifndef TIKU_POSIX_STORE_FDS
#define TIKU_POSIX_STORE_FDS 4
#endif
#define TIKU_POSIX_STORE_FD_BASE (TIKU_POSIX_FD_BASE + TIKU_POSIX_FDS)
/** @brief The largest file a descriptor writes: the size of one stage. */
#ifndef TIKU_POSIX_STAGE_MAX
#define TIKU_POSIX_STAGE_MAX 4096u
#endif
/** @brief Microseconds of store work one kernel turn gives a waiting call. */
#ifndef TIKU_POSIX_STORE_BUDGET_US
#define TIKU_POSIX_STORE_BUDGET_US 1000u
#endif
/** @brief Microseconds a change waits while a writer or a lease outside the
 *         library holds the store, before EBUSY; it waits for the library's
 *         own changes without a limit. */
#ifndef TIKU_POSIX_STORE_WAIT_US
#define TIKU_POSIX_STORE_WAIT_US 10000000u
#endif

/**
 * @brief Give the store group memory for its stages, TIKU_POSIX_STAGE_MAX
 *        bytes each, one for each descriptor that writes.
 * @return 0; EINVAL for NULL or less than one stage; EBUSY when given already
 */
int tiku_posix_store_memory(void *base, size_t bytes);
/** @brief Write into a descriptor's stage at its offset, or its end with
 *         O_APPEND; as much as the stage holds, then EFBIG. */
ssize_t tiku_posix_write(int fd, const void *buf, size_t n);
/** @brief Publish a descriptor's unpublished writes; 0 at once when there
 *         are none.  For a directory of the store, 0 while the store is
 *         mounted. */
int tiku_posix_fsync(int fd);
/** @brief Give @p from's content the name @p to, replacing a file there;
 *         both in the store. */
int tiku_posix_rename(const char *from, const char *to);
/** @brief Delete a file of the store. */
int tiku_posix_unlink(const char *path);
/** @brief Changes the group made durable, and those of them whose result
 *         a remount of the store decided after a failed request; either
 *         argument may be NULL. */
void tiku_posix_store_use(unsigned *published, unsigned *settled);

/*---------------------------------------------------------------------------*/
/* THREADS, MUTEXES AND CONDITION VARIABLES                                  */
/*---------------------------------------------------------------------------*/

/** @brief Threads the library starts at once; each is a kernel worker, so
 *         TIKU_THREADS_MAX bounds them with every other worker. */
#ifndef TIKU_POSIX_THREADS
#define TIKU_POSIX_THREADS 2
#endif
/** @brief Mutexes and condition variables live at once. */
#ifndef TIKU_POSIX_MUTEXES
#define TIKU_POSIX_MUTEXES 16
#endif
#ifndef TIKU_POSIX_CONDS
#define TIKU_POSIX_CONDS 8
#endif
/** @brief Locks the C library makes for its streams; past them its streams
 *         share one. */
#ifndef TIKU_POSIX_LOCKS
#define TIKU_POSIX_LOCKS 16
#endif

/** @brief Mutex types: a second lock by the holder is EDEADLK for NORMAL
 *         and ERRORCHECK, another hold for RECURSIVE. */
#define TIKU_POSIX_MUTEX_NORMAL     0
#define TIKU_POSIX_MUTEX_RECURSIVE  1
#define TIKU_POSIX_MUTEX_ERRORCHECK 2

/*
 * Handles are 32-bit: 0 or 0xFFFFFFFF (the C library's static initialiser)
 * has no object yet and takes one at first use.  These return 0 or an error
 * number and set no errno, as the pthread calls do.  None allocates.  A call
 * that would wait on the kernel thread returns EAGAIN there instead; on a
 * worker it waits on a kernel wait queue.  Call none inside an atomic
 * section.
 */

/** @brief The caller's identity: its control block's address, or 1 for the
 *         kernel thread. */
uint32_t tiku_posix_self(void);

/** @brief Mutexes and condition variables in use, and the objects refused
 *         because their table was full.  A caller that ignores a refused
 *         lock runs unlocked, so a program shows the count; any argument
 *         may be NULL. */
void tiku_posix_thread_use(unsigned *mutexes, unsigned *conds, unsigned *refused);

/** @brief A recursive lock kept in the caller's storage rather than the
 *         table: all zeros is a free lock, so a static one needs no setup. */
typedef struct {
    uint32_t opaque[3];
} tiku_posix_lock_t;
/** @brief Take @p lock, or hold it once more; EBUSY with @p try_only while
 *         another holds it. */
int tiku_posix_lock_acquire(tiku_posix_lock_t *lock, int try_only);
/** @brief Release one hold; EPERM unless the caller holds @p lock. */
int tiku_posix_lock_release(tiku_posix_lock_t *lock);
int tiku_posix_mutex_init(uint32_t *h, int type);
int tiku_posix_mutex_destroy(uint32_t *h);
/** @brief Take @p h, waiting while another holds it unless @p try_only, when
 *         that is EBUSY. */
int tiku_posix_mutex_lock(uint32_t *h, int try_only);
/** @brief EPERM unless the caller holds @p h. */
int tiku_posix_mutex_unlock(uint32_t *h);
int tiku_posix_cond_init(uint32_t *h);
int tiku_posix_cond_destroy(uint32_t *h);
/** @brief Release @p mh, wait for a signal, and take @p mh again; the caller
 *         holds @p mh once.  A wakeup without a signal is possible. */
int tiku_posix_cond_wait(uint32_t *ch, uint32_t *mh);
/** @brief Wake one waiter, or every waiter with @p all. */
int tiku_posix_cond_signal(uint32_t *h, int all);
/**
 * @brief The memory thread stacks are carved from: TIKU_POSIX_THREADS equal
 *        parts of @p bytes from @p base, 16-byte aligned.  The library has
 *        no other; until it is given, thread creation is EAGAIN.
 * @return 0; EINVAL for a part below TIKU_THREAD_STACK_MIN; EBUSY while a
 *         thread lives.
 */
int tiku_posix_thread_memory(void *base, size_t bytes);
/** @brief Start @p fn(@p arg) on a kernel worker; @p stack is the least it
 *         needs, 0 for any.  EAGAIN with no memory, part or worker slot. */
int tiku_posix_thread_create(uint32_t *id, size_t stack, int detached,
                             void *(*fn)(void *), void *arg);
/** @brief Wait for @p id to finish and take its result; its slot is free. */
int tiku_posix_thread_join(uint32_t id, void **result);
int tiku_posix_thread_detach(uint32_t id);
/** @brief End the calling thread with @p result; does not return. */
void tiku_posix_thread_exit(void *result);

/*---------------------------------------------------------------------------*/
/* THE HEAP                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * The kernel keeps no general heap.  A program written for POSIX gives the
 * library memory it holds from the kernel, and the library's heap group
 * (TIKU_POSIX_HEAP=1) hands it to the C library's allocator as the heap,
 * with the allocator's lock: a short interrupt-masked section, so any
 * thread, the kernel thread included, may allocate.  These return 0 or an
 * error number and set no errno; none waits.
 */

/** @brief The least heap tiku_posix_heap_memory() takes, after alignment. */
#define TIKU_POSIX_HEAP_MIN 4096u

/**
 * @brief Make @p bytes at @p base the heap, from its first 16-byte boundary;
 *        until then every growth is refused.
 * @return 0; EINVAL for NULL or less than TIKU_POSIX_HEAP_MIN bytes; EBUSY
 *         when the heap was given already.
 */
int tiku_posix_heap_memory(void *base, size_t bytes);
/** @brief Move the heap's break by @p increment bytes.
 *  @return The break before the move; (void *)-1, changing nothing, when no
 *          heap was given or the move would leave it.  A refused growth is
 *          counted. */
void *tiku_posix_heap_grow(intptr_t increment);
/** @brief Bytes below the break, bytes given, growths refused and the
 *         largest of them; any argument may be NULL. */
void tiku_posix_heap_use(size_t *used, size_t *limit, uint32_t *refused, size_t *largest);

/**
 * @brief The VFS caller capability (TIKU_VFS_CAP_* bits) the library's calls
 *        run under, set for each call and the caller's restored on every
 *        return.
 * @return The previous one.  TIKU_VFS_CAP_NONE (0) until set.
 */
uint8_t tiku_posix_authority_set(uint8_t cap);

#endif /* TIKU_POSIX_H_ */
