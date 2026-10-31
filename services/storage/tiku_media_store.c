/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_store.c - Serialized filesystem requests and borrowed-buffer completion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_store.h"
#include <string.h>

int tiku_media_store_init(tiku_media_store_t *s,tiku_mfs_t *fs,const tiku_block_t *block,
                          const uint8_t uuid[16],void *used,void *mark,size_t capacity,
                          void *backend,int (*acquire)(void *),int (*release)(void *))
{
    if(!s||!fs||!block||!uuid||!used||!mark||!block->read||!block->write||!block->sync||
       (!!acquire!=!!release)||!tiku_mfs_bitmap_bytes(block->sectors)||
       capacity<tiku_mfs_bitmap_bytes(block->sectors))return TIKU_MFS_ARG;
    memset(s,0,sizeof *s);s->fs=fs;s->block=*block;s->used=used;s->mark=mark;s->capacity=capacity;
    memcpy(s->uuid,uuid,16);s->backend=backend;s->acquire=acquire;s->release=release;
    tiku_mfs_init(fs);return 0;
}
/** @brief Map an operation to its required trusted-caller permission. */
static uint32_t permission(tiku_media_op_t op)
{
    if(op==TIKU_MEDIA_SEEK)return TIKU_MEDIA_READ;
    if(op==TIKU_MEDIA_MOUNT||op==TIKU_MEDIA_FORMAT||op>=TIKU_MEDIA_CHECKPOINT)return TIKU_MEDIA_ADMIN;
    if(op>=TIKU_MEDIA_OPEN&&op<=TIKU_MEDIA_LIST)return TIKU_MEDIA_READ;
    return TIKU_MEDIA_WRITE;
}
int tiku_media_store_submit(tiku_media_store_t *s,tiku_media_request_t *r)
{
    if(!s||!r||!s->fs||!r->client||r->op<TIKU_MEDIA_MOUNT||r->op>TIKU_MEDIA_SEEK||
       !(r->permissions&permission(r->op)))return TIKU_MFS_ARG;
    if(s->failed)return TIKU_MFS_IO;
    if(s->count==TIKU_MEDIA_QUEUE||s->running)return TIKU_MFS_BUSY;
    for(unsigned i=0;i<s->count;i++)if(s->queue[i]==r)return TIKU_MFS_BUSY;
    r->done=0;s->queue[s->count++]=r;return 0;
}
/** @brief Locate a reader owned by the requesting client. */
static int owned_reader(tiku_media_store_t *s,tiku_media_request_t *r)
{
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)
        if(s->readers[i].token==r->id&&r->id&&s->readers[i].client==r->client)return (int)i;
    return -1;
}
/** @brief Execute a request while its backend session is acquired. */
static int execute(tiku_media_store_t *s,tiku_media_request_t *r)
{
    int rc;int reader_index=owned_reader(s,r);
    if(r->op>=TIKU_MEDIA_APPEND&&r->op<=TIKU_MEDIA_RELEASE&&s->writer!=r->client)return TIKU_MFS_BUSY;
    if(s->copy.active&&r->op>=TIKU_MEDIA_APPEND&&r->op<=TIKU_MEDIA_RELEASE)return TIKU_MFS_BUSY;
    if((r->op==TIKU_MEDIA_COPY_STEP||r->op==TIKU_MEDIA_COPY_ABORT)&&s->writer!=r->client)return TIKU_MFS_BUSY;
    switch(r->op){
    case TIKU_MEDIA_MOUNT:case TIKU_MEDIA_FORMAT:
        if(s->writer||s->collector)return TIKU_MFS_BUSY;
        for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(s->readers[i].token)return TIKU_MFS_BUSY;
        if(r->op==TIKU_MEDIA_MOUNT)return tiku_mfs_mount(s->fs,&s->block,s->uuid,s->used,s->mark,s->capacity);
        return tiku_mfs_format(s->fs,&s->block,s->uuid,s->used,s->mark,s->capacity);
    case TIKU_MEDIA_BEGIN:
        rc=tiku_mfs_begin(s->fs,r->name,r->policy,r->offset,r->digest,&r->id);
        if(!rc)s->writer=r->client;
        return rc;
    case TIKU_MEDIA_RESUME:
        rc=tiku_mfs_resume(s->fs,r->id);if(!rc)s->writer=r->client;return rc;
    case TIKU_MEDIA_APPEND:return tiku_mfs_append(s->fs,r->buffer,r->length);
    case TIKU_MEDIA_SYNC:return tiku_mfs_sync(s->fs);
    case TIKU_MEDIA_FINALIZE:
        rc=tiku_mfs_finalize(s->fs);if(!rc)s->writer=0;return rc;
    case TIKU_MEDIA_RELEASE:
        rc=tiku_mfs_release(s->fs);if(!rc)s->writer=0;return rc;
    case TIKU_MEDIA_DELETE:return tiku_mfs_delete(s->fs,r->id);
    case TIKU_MEDIA_OPEN:
        for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(!s->readers[i].token){
            rc=tiku_mfs_open_read(s->fs,r->name,&r->id);
            if(!rc){s->readers[i].token=r->id;s->readers[i].client=r->client;}return rc;
        }
        return TIKU_MFS_LIMIT;
    case TIKU_MEDIA_READ_AT:
        if(reader_index<0)return TIKU_MFS_STALE;
        return tiku_mfs_read_at(s->fs,r->id,r->offset,r->buffer,r->length);
    case TIKU_MEDIA_CLOSE:
        if(reader_index<0)return TIKU_MFS_STALE;
        rc=tiku_mfs_close_read(s->fs,r->id);if(!rc)s->readers[reader_index].token=0;return rc;
    case TIKU_MEDIA_STAT:return tiku_mfs_stat(s->fs,r->name,r->info);
    case TIKU_MEDIA_SEEK:return tiku_mfs_list_seek(s->fs,r->name,&r->cursor);
    case TIKU_MEDIA_LIST:return tiku_mfs_list(s->fs,&r->cursor,r->info);
    case TIKU_MEDIA_UPLOADS:return tiku_mfs_list_pending(s->fs,&r->cursor,r->info);
    case TIKU_MEDIA_CHECKPOINT:return tiku_mfs_checkpoint(s->fs);
    case TIKU_MEDIA_COLLECT_BEGIN:
        rc=tiku_mfs_collect_begin(s->fs);if(!rc)s->collector=r->client;return rc;
    case TIKU_MEDIA_COLLECT_STEP:
        if(s->collector!=r->client)return TIKU_MFS_BUSY;
        rc=tiku_mfs_collect_step(s->fs,r->length);if(!rc)s->collector=0;return rc;
    case TIKU_MEDIA_COPY_BEGIN:
        rc=tiku_mfs_copy_begin(s->fs,&s->copy,r->name);if(!rc)s->writer=r->client;return rc;
    case TIKU_MEDIA_COPY_STEP:
        rc=tiku_mfs_copy_step(s->fs,&s->copy,r->buffer,r->length);
        if(!rc)s->writer=0;
        return rc;
    case TIKU_MEDIA_COPY_ABORT:
        rc=tiku_mfs_copy_abort(s->fs,&s->copy);if(!rc)s->writer=0;return rc;
    }
    return TIKU_MFS_ARG;
}
int tiku_media_store_step(tiku_media_store_t *s)
{
    if(!s||!s->fs)return TIKU_MFS_ARG;
    if(s->running)return TIKU_MFS_BUSY;
    if(!s->count)return 0;
    s->running=1;tiku_media_request_t *r=s->queue[0];
    int rc=s->failed?TIKU_MFS_IO:0;
    int session=r->op!=TIKU_MEDIA_CLOSE&&r->op!=TIKU_MEDIA_RELEASE;
    if(!rc&&session&&s->acquire){
        rc=s->acquire(s->backend);
        if(rc==TIKU_MFS_BUSY){s->running=0;return TIKU_MFS_BUSY;}
        if(rc){s->failed=s->fs->failed=1;rc=TIKU_MFS_IO;}
    }
    if(!rc){
        rc=execute(s,r);
        if(session&&s->release&&s->release(s->backend)){s->failed=s->fs->failed=1;rc=TIKU_MFS_IO;}
    }
    s->count--;for(unsigned i=0;i<s->count;i++)s->queue[i]=s->queue[i+1];
    r->result=rc;r->done=1;s->running=0;return 1;
}
