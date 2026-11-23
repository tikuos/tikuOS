# compat/posix/

The POSIX library: C-library system calls and the POSIX calls programs
written for it make, answered by TikuOS's own services. A program built for
a computer, such as the desktop TikuSurface runs, calls `open`, `rename`,
`opendir` and `clock_gettime`; this library answers them from the VFS, the
file store, the media store, the kernel clock and kernel threads. It sits
above `kernel/`: `kernel/`, `hal/` and `arch/` include nothing from it, and
`make lint` (`tools/check_kernel_dependencies.py`) refuses an include that
does.

The time group, the read group, the threads group and the heap group are
implemented. The other calls are in TikuSurface's
`desktop/posix.c`, over a tree in the desktop's heap, and move here a group
at a time with their backing changed to the kernel's services. The rules
below hold for each group as it lands.

## Building

`posix.mk` lists the sources by group. A port's makefile includes it when
`TIKU_POSIX_ENABLE` is 1 and adds `POSIX_SRCS` to its sources; with the
variable unset the image is the one built without this directory. The
S31 refuses `TIKU_*_ENABLE` on the make command line: its `posix-test`
profile sets the variable, and an application manifest such as
TikuSurface's sets it for its image. The time group needs the port's
microsecond clock (`TIKU_CLOCK_ARCH_USEC` in its `tiku_timer_arch.h`).

`tiku_posix_hooks.c` defines the C library's file hooks (`_open`, `_read`,
`_lseek`, `_close`, `_fcntl`, `_fstat`, `_stat`, `_isatty`) and `opendir`,
`readdir`, `rewinddir` and `closedir` over the read group. A program with
a file layer of its own, which keeps those hooks, sets
`TIKU_POSIX_FILE_HOOKS` to 0 and calls the `tiku_posix_*` names in
`tiku_posix.h` for the paths it hands the kernel: TikuSurface routes
`/Board` so.

## Groups

A group is a set of calls that land together and that a build selects
together: time; descriptors and files; directories; durable stores (`fsync`,
`rename`); threads and locks. For each call this file lists the newlib hook
it defines, the kernel operation behind it, the flags and limits it
supports, the errors it returns and the test that covers it. A call the
library does not support returns its documented error if a program links
it; it never succeeds without doing its work. A constant process id, a lock
that does nothing or an `fsync` that writes nothing is not an
implementation.

## The time group

`tiku_posix_time.c`. None of these calls waits: a process and a worker
thread may make each of them.

| Call | newlib hook | Kernel operation | Supports | Errors |
| --- | --- | --- | --- | --- |
| `clock_gettime` | itself (libnosys has a weak one returning `ENOSYS`) | `CLOCK_MONOTONIC`, `_RAW`, `_COARSE`: `tiku_clock_usec()`. `CLOCK_REALTIME`, `_COARSE`: `tiku_rtc_read()`, whole seconds, while the wall clock is set or resumed; while it is unset, the time since start | microseconds; real time in seconds | `EINVAL` for another clock or a NULL result, which is not written |
| `clock_getres` | itself | none | 1 µs monotonic, 1 s real time | `EINVAL` for another clock |
| `gettimeofday`, `time` | `_gettimeofday` | `clock_gettime(CLOCK_REALTIME)` | microseconds; `tz` ignored | none; a NULL `tv` is not written |

POSIX has no word for a wall clock that is set but not current. A program
that shows the date as current asks the kernel: `tiku_rtc_state()` is
`TIKU_RTC_SET` only for a time set during this boot, and
`TIKU_RTC_RESUMED` for one carried over a reset.

Tests: TikuBench `tests/host/kernel/test_posix_time.c` and
`test_rtc_state.c` (`make posix rtc-state`, mutation list
`mutations/posix.py`), and the S31 `posix-test` profile through
`make ram-load` (`tools/esp32s31_ram.py --posix-test`).

## The read group

`tiku_posix_vfs.c` over the kernel VFS: `tiku_vfs_read_at()` for reads and
`tiku_vfs_list()` for directory streams.  Every node is read-only.  None of
these calls waits.  Paths are absolute VFS paths shorter than
`TIKU_VFS_PATH_MAX` (64); there is no working directory.

| Call | Kernel operation | Supports | Errors |
| --- | --- | --- | --- |
| `open` | `tiku_vfs_is_dir()`, `tiku_vfs_resolve()`; a dynamic directory's child by a 1-byte read | `O_RDONLY`, on a file or a directory, `O_DIRECTORY`; `TIKU_POSIX_FDS` (8) descriptors from 3, over every caller | `ENOENT` (relative path too), `ENAMETOOLONG`, `EROFS` for a write mode, `O_CREAT`, `O_TRUNC` or `O_APPEND`, `EISDIR` for a directory opened to write, `EACCES` for a node with no read, `ENOTDIR`, `EMFILE` |
| `read` | `tiku_vfs_read_at()` at the descriptor's offset | up to `TIKU_POSIX_READ_MAX` (256) bytes a call; a node the kernel renders whole gives its first 255 bytes | `EBADF`, `EISDIR`, `EIO` past a whole render's end, the node's error mapped |
| `lseek` | none | `SEEK_SET`, `SEEK_CUR` | `EBADF`, `EINVAL` for `SEEK_END`, a negative offset or a directory |
| `close` | none | its number is free for the next open | `EBADF` |
| `fcntl` | none | `F_GETFL` (`O_RDONLY`), `F_GETFD`, `F_SETFD` | `EBADF`, `EINVAL` |
| `fstat`, `stat` | as `open` | mode, `st_ino` a number from the path, the same as `readdir`'s `d_ino`; size and `st_dev` 0 | as `open` |
| `opendir`, `readdir`, `rewinddir`, `closedir` | `tiku_vfs_list()`, from the start for each entry | `TIKU_POSIX_DIRS` (4) streams; static children and mounts, then a dynamic directory's | `ENOENT`, `ENOTDIR`, `EMFILE`, `EBADF`; the end leaves `errno` |

Opening a static node does not read it, so a node whose read consumes,
such as `/sys/vfs/events`, is untouched until `read`.  A descriptor number
closed by one thread can be opened again by another before the first
thread's next call.  Each call runs under the library's VFS authority,
`tiku_posix_authority_set()` (none until set), and the caller's is back on
every return, a failure's included.

Tests: TikuBench `tests/host/kernel/test_posix_vfs.c` over the real VFS
core and a fixed tree (`make posix`, `mutations/posix.py`), and the S31
application `tests/esp32s31/posix_app/` on the kernel's namespace: built
with `S31_PROFILE=s31-local` and that directory's `posix_app.mk` as the
application manifest, installed in the application span, judged by its
`check.py` from the console log.

## The threads group

`tiku_posix_thread.c` keeps threads, mutexes and condition variables behind
32-bit handles on kernel workers and wait queues; `tiku_posix_pthread.c`
gives them newlib's pthread names, whose `pthread_t`, `pthread_mutex_t` and
`pthread_cond_t` are 32-bit handles.  Nothing allocates: mutexes
(`TIKU_POSIX_MUTEXES`, 16) and conditions (`TIKU_POSIX_CONDS`, 8) come from
tables, and a handle still holding the static initialiser takes an object at
its first use.

| Call | Kernel operation | Supports | Errors |
| --- | --- | --- | --- |
| `pthread_create`, `_join`, `_detach`, `_exit`, `_self`, `_equal`, `pthread_attr_*` (detach state, stack size) | `tiku_thread_start()` through `tiku_thread_call()`; the join waits on a wait queue | `TIKU_POSIX_THREADS` (2) threads, on stacks carved from the memory given to `tiku_posix_thread_memory()`; a slot is reused once the kernel marks its worker done | `EAGAIN` with no memory, part or worker slot, `ESRCH`, `EINVAL` for a detached thread, `EDEADLK` joining oneself |
| `pthread_mutex_init`, `_destroy`, `_lock`, `_trylock`, `_unlock`, `pthread_mutexattr_*` | a wait queue, under `tiku_atomic_enter()` | normal, recursive, error-checking | `EBUSY` (trylock, destroy while held), `EDEADLK` for a second normal or error-checking lock, `EPERM` unlocking another's, `EAGAIN` with the table full |
| `pthread_cond_init`, `_destroy`, `_wait`, `_signal`, `_broadcast` | a wait queue and a signal count | a wakeup without a signal is possible, as POSIX allows | `EPERM` waiting without the mutex, `EBUSY` destroying one waited on |

On the kernel thread a call that would wait, a held mutex's lock, a
condition wait or the join of a running thread, returns `EAGAIN` and
changes nothing; a process waits for a thread with `TIKU_WAIT_WORKER()` on
its control block (`pthread_t` is that block's address).  None of these is
called inside an atomic section.  `errno` is per thread through the port.
A program that ignores a lock's `EAGAIN` for a full table runs unlocked:
`tiku_posix_thread_use()` gives the objects in use and the refusals, for a
program to show.

`tiku_posix_lock.c` defines newlib's retargetable locks, which its stdio
streams, environment, time zone and exit handlers take, as recursive locks
kept in the lock objects themselves (`tiku_posix_lock_t`), not in the
mutex table.  Defining all of them keeps newlib's own, which do nothing,
out of the link.  Each stream newlib opens takes one of
`TIKU_POSIX_LOCKS` (16); streams opened past them share one lock.  The
kernel thread cannot wait for a worker holding one of these locks, and
running on would break what it guards, so that acquire traps
(`__builtin_trap()`; the S31 reports the trap and stops): the kernel thread
does not use the C library's streams, environment or time zone while
workers do.

Tests: TikuBench `test_posix_thread.c` over stand-in workers (`make posix`,
`mutations/posix.py`), and the threads phase of the S31 application in
`tests/esp32s31/posix_app/`: refusals on the kernel thread, two threads
taking one mutex 200 times each across yields with no overlap, a producer
handing a consumer 100 values through a condition, each thread's `errno`
its own, and `getenv()` and a stream's lock each waiting while another
thread holds them.

## The heap group

The kernel keeps no heap: `kernel/memory` has arenas, freed whole, and pools
of fixed blocks, and the C library's `_sbrk()` refuses
(`kernel/memory/tiku_noheap.c`; the S31's own in
`arch/esp32s31/tiku_libc_heap.c`). A program written for POSIX that frees
blocks of any size one at a time runs the C library's allocator inside one
arena it holds from the kernel; that heap is the program's runtime, as its
stack is. Selected with `TIKU_POSIX_HEAP=1` (off by default; with it off
the image does not change).

| Call | Kernel operation | Supports | Errors |
| --- | --- | --- | --- |
| `tiku_posix_heap_memory(base, bytes)` | none: the program gives memory it holds, such as a `tiku_tier_arena_create_span()` arena | once; from the first 16-byte boundary; at least `TIKU_POSIX_HEAP_MIN` (4096) bytes | `EINVAL`, `EBUSY` when given already |
| `_sbrk_r` (`tiku_posix_sbrk.c`), reached by `malloc` and the calls that allocate | `tiku_posix_heap_grow()`, inside an atomic section | growth and shrinking inside the memory given | `ENOMEM` past it or before the memory is given; a refused growth is counted |
| `__malloc_lock`, `__malloc_unlock` (`tiku_posix_sbrk.c`) | `tiku_atomic_enter()`, `tiku_atomic_exit()` | nested | none |
| `tiku_posix_heap_use()` | none | bytes below the break, bytes given, refused growths and the largest; does not wait | none |

The allocator's lock is an interrupt-masked section, not one of the
library's locks: the kernel thread cannot wait for a thread, and with the
masked section any thread, the kernel thread included, may allocate.
newlib's `malloc` and `free` update their lists inside it; its `realloc`
copies outside it. In TikuSurface's desktop the longest hold seen over
1.85 million allocations was 750 µs; one lock and release costs 1.25 µs on
the S31, against 7.3 µs for the library's recursive lock.

Tests: TikuBench `test_posix_heap.c` (`make posix`, `mutations/posix.py`),
and the heap phase of the S31 application in `tests/esp32s31/posix_app/`:
`malloc` fails before memory is given, two threads allocate 600 blocks each
with none handed out twice, growth past the heap is refused and counted, a
stream on a kernel node, the kernel thread allocating while a thread is
inside the allocator, and the timer on time while `realloc` copies 1 MiB
blocks.

## Calls from a worker

A worker's call reaches the kernel thread through `tiku_thread_call()`
(`kernel/threads/tiku_thread.h`): the call queues a record on the worker's
own stack, wakes the kernel and waits; the scheduler's dispatch runs it
between processes, and `tiku_sched_has_pending()` counts it, so the kernel
does not idle past it.  Each worker has at most one call queued.  From the
kernel thread the call runs at once.  The library's calls all go this way,
so every VFS operation runs on the kernel thread.

## Calling context

The calls that wait (a store write, a media read, a lock) are for worker
threads. A call made on a worker hands its request to the kernel thread,
which runs it and wakes the worker with the result. From the kernel thread
only a call that completes without waiting is allowed, such as a read of a
VFS node or of a stored file; any other returns `EAGAIN` there and changes
nothing. In the time and read groups no call waits; in the threads group a
contended lock, a condition wait and a join do.

## Memory

The library allocates nothing. Its descriptor, mount and directory-stream
tables are static, with sizes set by build options. A program that wants
calls which allocate, such as `fopen` or `tmpfile`, selects the heap group
and gives it memory, as TikuSurface's desktop does with part of its PSRAM
tier arena.

## Threads

`errno` must be per thread. The S31 port gives each worker its own newlib
reentrancy structure (`arch/esp32s31/tiku_runtime_arch.c`); a port that
selects the library with workers does the same.

## Proof

Each group lands with a TikuBench host suite that runs its calls against
stand-in providers, with a mutation list, and a board suite that builds and
runs two programs through the runner: one on a worker thread, which makes
the waiting calls, and one in a process, which makes the calls allowed
there and is refused the rest. The program that needs the group links the
same implementation. An image built without the library is byte-identical
to one built from a tree without this directory; remove the build directory
and `main.elf` before comparing.
