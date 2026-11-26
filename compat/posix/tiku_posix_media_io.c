/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_posix_media_io.c - queued reads and mutations on mounted media.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_posix_media_io.h"
#include <services/storage/tiku_media_card.h>
#include <string.h>
/** @brief Queue storage remains live through service completion and reader closure. */
typedef struct {
    tiku_media_store_t *store;
    tiku_media_request_t request;
    tiku_mfs_object_t info;
    tiku_posix_media_io_t *view;
    _Alignas(8) unsigned char read_state[TIKU_POSIX_MEDIA_READ_BYTES];
    _Alignas(8) unsigned char mutation_state[TIKU_POSIX_MEDIA_MUTATION_BYTES];
    uint32_t client, permissions;
    unsigned ready;
    unsigned answered;          /* the view was answered at its submission */
    uint64_t known;             /* the card's names epoch, accounted for */
    unsigned foreign;  /* name changes this bridge did not make */
} media_bridge_t;
_Static_assert(sizeof(media_bridge_t)<=TIKU_POSIX_MEDIA_BRIDGE_BYTES,"Media bridge exceeds runtime reservation");

size_t tiku_posix_media_queue_size(void)
{ return tiku_media_card_read_size() <= TIKU_POSIX_MEDIA_READ_BYTES && tiku_media_card_mutation_size() <= TIKU_POSIX_MEDIA_MUTATION_BYTES ? sizeof(media_bridge_t) : SIZE_MAX; }
void tiku_posix_media_queue_init(void *data, uint32_t client, unsigned permissions)
{
    media_bridge_t *b = data;
    memset(b, 0, sizeof *b);
    b->client = client;
    b->permissions = permissions;
    b->ready = client && tiku_media_card_read_size() <= sizeof b->read_state &&
        tiku_media_card_mutation_size() <= sizeof b->mutation_state;
    if (b->ready) {
        tiku_media_card_read_init(b->read_state);
        tiku_media_card_mutation_init(b->mutation_state);
    }
}
/** @brief Convert media results while keeping stale identity distinct from removal. */
static int status(int r)
{
    if (!r) return TIKU_POSIX_MEDIA_OK;
    if (r == TIKU_MFS_NOENT) return TIKU_POSIX_MEDIA_END;
    if (r == TIKU_MFS_BUSY) return TIKU_POSIX_MEDIA_BUSY;
    if (r == TIKU_MFS_STALE) return TIKU_POSIX_MEDIA_STALE;
    if (r == TIKU_MFS_ARG) return TIKU_POSIX_MEDIA_INVALID;
    if (r == TIKU_MFS_LIMIT) return TIKU_POSIX_MEDIA_LIMIT;
    if (r == TIKU_MFS_SPACE) return TIKU_POSIX_MEDIA_FULL;
    if (r == TIKU_MFS_UNSUPPORTED) return TIKU_POSIX_MEDIA_UNSUPPORTED;
    return TIKU_POSIX_MEDIA_UNAVAILABLE;
}

/** @brief Account for the card's names epoch: any change since the last
 *  account beyond @p own of this bridge's own was another client's. */
static void account(media_bridge_t *b, uint64_t own)
{
    tiku_media_store_t *s = tiku_media_card_store();
    uint64_t e = s && s->fs ? tiku_mfs_names_epoch(s->fs) : 0;

    if (e == 0) return;
    if (b->known != 0 && e != b->known && e - b->known > own)
        __atomic_add_fetch(&b->foreign, 1u, __ATOMIC_RELEASE);
    b->known = e;
}

/** @brief Answer a space request from the mounted volume: its bytes, and
 *  the bytes a writer may use past the catalog's reserve. */
static int space(media_bridge_t *b, tiku_posix_media_io_t *v)
{
    tiku_media_store_t *s = tiku_media_card_store();
    if (!s || !s->fs || !s->fs->mounted) return TIKU_POSIX_MEDIA_UNAVAILABLE;
    const tiku_mfs_t *f = s->fs;
    uint64_t sectors = (uint64_t)f->free_segments * TIKU_MFS_SEGMENT +
        (f->tail % TIKU_MFS_SEGMENT ? TIKU_MFS_SEGMENT - f->tail % TIKU_MFS_SEGMENT : 0);
    v->offset = (uint64_t)f->segments * TIKU_MFS_SEGMENT * 512u;
    v->size = sectors > TIKU_MFS_SEGMENT ? (sectors - TIKU_MFS_SEGMENT) * 512u : 0;
    v->result = TIKU_POSIX_MEDIA_OK;
    b->answered = 1;
    b->view = v;
    return TIKU_POSIX_MEDIA_OK;
}

/** @brief The firmware operation behind bridge operation @p op, or -1 when
 *  this build does not serve it. */
static int media_op(unsigned op)
{
    static const tiku_media_op_t base[] = {TIKU_MEDIA_ENUMERATE, TIKU_MEDIA_OPEN_VERSION,
        TIKU_MEDIA_READ_AT, TIKU_MEDIA_CLOSE, TIKU_MEDIA_RENAME, TIKU_MEDIA_DELETE_VERSION, TIKU_MEDIA_REPLACE
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        ,TIKU_MEDIA_MKDIR,TIKU_MEDIA_TREE_RENAME,TIKU_MEDIA_TREE_DELETE,TIKU_MEDIA_DIRECTORY_UPGRADE,TIKU_MEDIA_TREE_COPY
#endif
        };
    static const tiku_media_op_t stream[] = {TIKU_MEDIA_BEGIN, TIKU_MEDIA_RESUME, TIKU_MEDIA_APPEND,
        TIKU_MEDIA_SYNC, TIKU_MEDIA_FINALIZE, TIKU_MEDIA_RELEASE, TIKU_MEDIA_DISCARD, TIKU_MEDIA_RENAME_OVER};
    if (op < sizeof base / sizeof base[0]) return (int)base[op];
    if (op >= TIKU_POSIX_MEDIA_BEGIN && op <= TIKU_POSIX_MEDIA_RENAME_OVER) return (int)stream[op - TIKU_POSIX_MEDIA_BEGIN];
    return -1;
}

/** @brief Whether firmware operation @p op runs on the mutation owner. */
static int mutating(int op)
{
    return op == TIKU_MEDIA_RENAME || op == TIKU_MEDIA_DELETE_VERSION || op == TIKU_MEDIA_REPLACE ||
        (op >= TIKU_MEDIA_BEGIN && op <= TIKU_MEDIA_RELEASE) ||
        op == TIKU_MEDIA_DISCARD || op == TIKU_MEDIA_RENAME_OVER
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        || op == TIKU_MEDIA_MKDIR || op == TIKU_MEDIA_TREE_RENAME || op == TIKU_MEDIA_TREE_DELETE ||
        op == TIKU_MEDIA_DIRECTORY_UPGRADE || op == TIKU_MEDIA_TREE_COPY
#endif
        ;
}

int tiku_posix_media_queue_submit(void *data, tiku_posix_media_io_t *v)
{
    media_bridge_t *b = data;
    if (!b || !v) return TIKU_POSIX_MEDIA_INVALID;
    if (!b->ready) return TIKU_POSIX_MEDIA_UNAVAILABLE;
    if (b->view) return TIKU_POSIX_MEDIA_BUSY;
    account(b, 0);
    if (v->operation == TIKU_POSIX_MEDIA_SPACE) {
        if (!(b->permissions & TIKU_MEDIA_READ)) return TIKU_POSIX_MEDIA_DENIED;
        return space(b, v);
    }
    int op = media_op(v->operation);
    int bulk = v->operation == TIKU_POSIX_MEDIA_REPLACE || v->operation == TIKU_POSIX_MEDIA_TREE_COPY ||
               v->operation == TIKU_POSIX_MEDIA_APPEND || v->operation == TIKU_POSIX_MEDIA_READ;
    if (op < 0 || v->length > (bulk ? TIKU_POSIX_MEDIA_CHUNK : 1024u) ||
        !memchr(v->name, 0, sizeof v->name) || !memchr(v->destination, 0, sizeof v->destination))
        return TIKU_POSIX_MEDIA_INVALID;

    int rename = v->operation == TIKU_POSIX_MEDIA_RENAME || v->operation == TIKU_POSIX_MEDIA_TREE_RENAME ||
                 v->operation == TIKU_POSIX_MEDIA_RENAME_OVER;
    unsigned permissions = mutating(op) ? TIKU_MEDIA_WRITE : TIKU_MEDIA_READ;
    if (v->operation == TIKU_POSIX_MEDIA_DIRECTORY_UPGRADE) permissions = TIKU_MEDIA_ADMIN;
    if (v->operation == TIKU_POSIX_MEDIA_TREE_COPY) permissions |= TIKU_MEDIA_READ;
    if ((b->permissions & permissions) != permissions) return TIKU_POSIX_MEDIA_DENIED;
    if (v->operation != TIKU_POSIX_MEDIA_READ && v->operation != TIKU_POSIX_MEDIA_CLOSE)
        b->store = tiku_media_card_store();
    if (!b->store) return TIKU_POSIX_MEDIA_UNAVAILABLE;
    b->request = (tiku_media_request_t){
        .op = (tiku_media_op_t)op, .client = b->client, .permissions = permissions,
        .name = v->name, .names_epoch = v->epoch, .rank = v->rank,
        .exclusive = (uint8_t)v->exclusive, .info = &b->info,
        .id = mutating(op) ? v->id : v->reader, .offset = v->offset,
        .buffer = rename ? (void *)v->destination : v->bytes,
        .length = (uint32_t)v->length, .policy = TIKU_MFS_STREAM
    };
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if (v->operation == TIKU_POSIX_MEDIA_TREE_COPY) b->request.destination = v->destination;
#endif
    int r = tiku_media_store_submit(b->store, &b->request);
    if (!r) b->view = v;
    return status(r);
}

/** @brief Report retirement of both shared-queue owners before releasing bridge memory. */
static int idle(const media_bridge_t *b)
{ return tiku_media_card_read_idle(b->read_state) && tiku_media_card_mutation_idle(b->mutation_state); }
/** @brief Advance the active owner or dispatch the FIFO head without synchronous I/O. */
static void progress(media_bridge_t *b)
{
    if (!tiku_media_card_read_idle(b->read_state))
        (void)tiku_media_card_read_step(b->read_state, b->store);
    else if (!tiku_media_card_mutation_idle(b->mutation_state) ||
             (b->store->count && mutating((int)b->store->queue[0]->op)))
        (void)tiku_media_card_mutation_step(b->mutation_state, b->store);
    else (void)tiku_media_card_read_step(b->read_state, b->store);
}

int tiku_posix_media_queue_poll(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready) return 0;
    if (!b->view) {
        if (b->store && b->store->count) progress(b);
        return 0;
    }
    if (b->answered) {
        b->answered = 0;
        b->view = NULL;
        return 1;
    }
    if (!b->request.done || !idle(b)) progress(b);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
    if (b->view->operation == TIKU_POSIX_MEDIA_TREE_COPY) {
        b->view->prepared = b->request.prepared;
        b->view->total = b->request.total;
    }
#endif
    if (!b->request.done || !idle(b)) return 0;
    tiku_posix_media_io_t *v = b->view;
    int r = b->request.result;
    v->result = status(r);
    if (v->operation == TIKU_POSIX_MEDIA_READ && r >= 0) {
        v->count = (size_t)r; v->result = TIKU_POSIX_MEDIA_OK;
    }
    if (v->operation == TIKU_POSIX_MEDIA_ENUMERATE && (r == 0 || r == TIKU_MFS_NOENT)) {
        v->epoch = b->request.names_epoch; v->rank = b->request.rank;
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        v->directory = !r && b->info.policy == TIKU_MFS_DIRECTORY;
#endif
        if (!r) {
            memcpy(v->found, b->info.name, sizeof v->found);
            v->size = b->info.length;
            v->id = b->info.id;
        }
    }
    if (v->operation == TIKU_POSIX_MEDIA_OPEN && !r) {
        v->reader = b->request.id; v->size = b->info.length;
        v->id = b->info.id;
    }
    if (v->operation >= TIKU_POSIX_MEDIA_RENAME && !r) {
        v->epoch = b->store->fs->names_epoch;
        v->id = b->request.id;
    }
    /* Each of these changes the names once; a resume, append, sync or
     * release none. */
    account(b, !r && ((v->operation >= TIKU_POSIX_MEDIA_RENAME && v->operation <= TIKU_POSIX_MEDIA_REPLACE) ||
                      v->operation == TIKU_POSIX_MEDIA_BEGIN || v->operation == TIKU_POSIX_MEDIA_FINALIZE ||
                      v->operation == TIKU_POSIX_MEDIA_DISCARD || v->operation == TIKU_POSIX_MEDIA_RENAME_OVER));
    b->view = NULL;
    return 1;
}

int tiku_posix_media_queue_idle(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready || b->view) return 0;
    account(b, 0);
    return tiku_media_card_park_expire();
}

unsigned tiku_posix_media_queue_foreign(const void *data)
{
    const media_bridge_t *b = data;
    return b ? __atomic_load_n(&b->foreign, __ATOMIC_ACQUIRE) : 0u;
}

int tiku_posix_media_queue_cancel(void *data)
{
    media_bridge_t *b = data;
    if (!b || !b->ready) return 0;
    if (b->answered) {
        b->answered = 0;
        b->view = NULL;
    }
    if (b->view && !b->request.done && b->view->operation != TIKU_POSIX_MEDIA_CLOSE) {
        int r = b->view->operation >= TIKU_POSIX_MEDIA_RENAME ?
            tiku_media_card_mutation_cancel(b->mutation_state, b->store, &b->request) :
            tiku_media_card_read_cancel(b->read_state, b->store, &b->request);
        if (r) return 0;
    }
    if (!idle(b)) progress(b);
    return idle(b);
}
