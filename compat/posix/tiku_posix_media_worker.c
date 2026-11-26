/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_posix_media_worker.c - blocking card access from a TikuOS worker.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_posix_media.h"
#include <kernel/threads/tiku_thread.h>
#include <errno.h>

typedef struct {
    void *queue;
    tiku_posix_media_io_t *io;
} media_call_t;

static int submit(void *arg)
{
    media_call_t *call = arg;
    return tiku_posix_media_queue_submit(call->queue, call->io);
}

static int poll(void *arg)
{
    media_call_t *call = arg;
    return tiku_posix_media_queue_poll(call->queue);
}

static void worker_request(void *queue, tiku_posix_media_io_t *io)
{
    media_call_t call = {queue, io};
    if (tiku_thread_in_kernel()) {
        io->result = TIKU_POSIX_MEDIA_AGAIN;
        return;
    }
    int status = tiku_thread_call(submit, &call);
    if (status != TIKU_POSIX_MEDIA_OK) {
        io->result = status;
        return;
    }
    while (!tiku_thread_call(poll, &call)) tiku_thread_yield();
}

int tiku_posix_media_connect(tiku_posix_media_t *volume, void *queue, size_t size,
                            uint32_t client, unsigned permissions)
{
    if (!volume || !queue || !client || ((uintptr_t)queue & 7u) ||
        size < tiku_posix_media_queue_size() || !tiku_thread_in_kernel()) {
        errno = EINVAL;
        return -1;
    }
    tiku_posix_media_queue_init(queue, client, permissions);
    *volume = (tiku_posix_media_t){worker_request, queue};
    return 0;
}
