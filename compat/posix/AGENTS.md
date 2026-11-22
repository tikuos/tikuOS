# compat/posix/

The POSIX library: C-library system calls and the POSIX calls programs
written for it make, answered by TikuOS's own services. A program built for
a computer, such as the desktop TikuSurface runs, calls `open`, `rename`,
`opendir` and `clock_gettime`; this library answers them from the VFS, the
file store, the media store, the kernel clock and kernel threads. It sits
above `kernel/`: `kernel/`, `hal/` and `arch/` include nothing from it, and
`make lint` (`tools/check_kernel_dependencies.py`) refuses an include that
does.

The time group is implemented. The other calls are in TikuSurface's
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

## Calling context

The calls that wait (a store write, a media read, a lock) are for worker
threads. A call made on a worker hands its request to the kernel thread,
which runs it and wakes the worker with the result. From the kernel thread
only a call that completes without waiting is allowed, such as a read of a
VFS node or of a stored file; any other returns `EAGAIN` there and changes
nothing.

## Memory

The library allocates nothing from the C library's heap, which TikuOS
refuses (`kernel/memory/tiku_noheap.c`). Its descriptor, mount and
directory-stream tables are static, with sizes set by build options. A
program that wants calls which allocate, such as `fopen` or `tmpfile`,
brings its own heap, as the desktop does with a PSRAM tier arena.

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
