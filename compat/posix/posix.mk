# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
#
# compat/posix: the POSIX library's sources, by group.  A port's makefile
# includes this file when TIKU_POSIX_ENABLE=1 and adds POSIX_SRCS to its
# sources; with the option unset nothing here reaches the build.  A program
# with its own file layer, which keeps the C library's file hooks, sets
# TIKU_POSIX_FILE_HOOKS to 0 and calls the tiku_posix_* names itself.
TIKU_POSIX_FILE_HOOKS ?= 1
# The heap group: a program that sets TIKU_POSIX_HEAP to 1 gives the library
# memory for the C library's heap (tiku_posix_heap_memory()).
TIKU_POSIX_HEAP ?= 0
# The store group: files under /data written through stages the program
# gives (tiku_posix_store_memory()) and published with the file store's
# polled writes.  POSIX_FLAGS carries it to the read group and the hooks.
TIKU_POSIX_STORE ?= 0
TIKU_POSIX_MEDIA ?= 0
POSIX_SRCS := compat/posix/tiku_posix_time.c compat/posix/tiku_posix_vfs.c \
    compat/posix/tiku_posix_thread.c compat/posix/tiku_posix_pthread.c \
    compat/posix/tiku_posix_lock.c
ifeq ($(TIKU_POSIX_FILE_HOOKS),1)
POSIX_SRCS += compat/posix/tiku_posix_hooks.c
endif
ifeq ($(TIKU_POSIX_HEAP),1)
POSIX_SRCS += compat/posix/tiku_posix_heap.c compat/posix/tiku_posix_sbrk.c
endif
POSIX_FLAGS :=
ifeq ($(TIKU_POSIX_STORE),1)
POSIX_SRCS += compat/posix/tiku_posix_store.c
POSIX_FLAGS += -DTIKU_POSIX_STORE=1 -DTIKU_TFS_ASYNC_ENABLE=1
endif
ifeq ($(TIKU_POSIX_MEDIA),1)
POSIX_SRCS += compat/posix/tiku_posix_media.c compat/posix/tiku_posix_media_io.c \
    compat/posix/tiku_posix_media_worker.c services/storage/tiku_media_async.c \
    services/storage/tiku_media_mutation.c services/storage/tiku_media_checkpoint.c
POSIX_FLAGS += -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1
endif
