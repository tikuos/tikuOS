# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
#
# compat/posix: the POSIX library's sources, by group.  A port's makefile
# includes this file when TIKU_POSIX_ENABLE=1 and adds POSIX_SRCS to its
# sources; with the option unset nothing here reaches the build.  A program
# with its own file layer, which keeps the C library's file hooks, sets
# TIKU_POSIX_FILE_HOOKS to 0 and calls the tiku_posix_* names itself.
TIKU_POSIX_FILE_HOOKS ?= 1
POSIX_SRCS := compat/posix/tiku_posix_time.c compat/posix/tiku_posix_vfs.c \
    compat/posix/tiku_posix_thread.c compat/posix/tiku_posix_pthread.c \
    compat/posix/tiku_posix_lock.c
ifeq ($(TIKU_POSIX_FILE_HOOKS),1)
POSIX_SRCS += compat/posix/tiku_posix_hooks.c
endif
