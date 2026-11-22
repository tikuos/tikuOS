# compat/posix/

The POSIX library: C-library system calls and the POSIX calls programs
written for it make, answered by TikuOS's own services. A program built for
a computer, such as the desktop TikuSurface runs, calls `open`, `rename`,
`opendir` and `clock_gettime`; this library answers them from the VFS, the
file store, the media store, the kernel clock and kernel threads. It sits
above `kernel/`: `kernel/`, `hal/` and `arch/` include nothing from it, and
`make lint` (`tools/check_kernel_dependencies.py`) refuses an include that
does.

No call is implemented here yet. The calls are in TikuSurface's
`desktop/posix.c`, over a tree in the desktop's heap, and move here a group
at a time with their backing changed to the kernel's services. The rules
below hold for each group as it lands.

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
