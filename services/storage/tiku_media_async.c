/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_async.c - Sector-paced read requests under shared queue serialization.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_async.h"
#include <string.h>
#if TIKU_MEDIA_ASYNC_ENABLE
/** @brief Locate a snapshot owned by the queued request's client. */
static int read_slot(tiku_media_store_t *s,const tiku_media_request_t *r)
{
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)
        if(r->id&&s->readers[i].token==r->id&&s->readers[i].client==r->client)return (int)i;
    return -1;
}
/** @brief Remove a completed request while preserving FIFO order for its successors. */
static void finish(tiku_media_store_t *s,unsigned index,int result)
{
    tiku_media_request_t *r=s->queue[index];
    s->count--;for(unsigned i=index;i<s->count;i++)s->queue[i]=s->queue[i+1];
    r->result=result;r->done=1;
}
void tiku_media_async_init(tiku_media_async_t *a,const tiku_media_async_ops_t *ops,void *context)
{ if(a)*a=(tiku_media_async_t){.ops=ops,.context=context}; }
/** @brief Forget a retired query while keeping its transport binding. */
static void retire(tiku_media_async_t *a)
{ a->store=NULL;a->request=NULL;a->phase=a->opened=0; }
/** @brief Construct the read query while the shared store excludes other execution. */
static int begin(tiku_media_async_t *a)
{
    tiku_media_store_t *s=a->store;tiku_media_request_t *r=a->request;
    if(s->failed)return TIKU_MFS_IO;
    if(r->op==TIKU_MEDIA_READ_AT){
        if(read_slot(s,r)<0)return TIKU_MFS_STALE;
        return tiku_mfs_query_read(s->fs,&a->query,r->id,r->offset,r->buffer,r->length);
    }
    if(!r->info||(r->op==TIKU_MEDIA_OPEN_VERSION&&!r->names_epoch))return TIKU_MFS_ARG;
    return tiku_mfs_query_enumerate(s->fs,&a->query,r->name,
        r->op==TIKU_MEDIA_ENUMERATE?r->exclusive:0,r->names_epoch);
}
/** @brief Publish checked metadata or a newly pinned snapshot after transport release. */
static int result(tiku_media_async_t *a)
{
    tiku_media_store_t *s=a->store;tiku_media_request_t *r=a->request;tiku_mfs_query_t *q=&a->query;
    int rc=a->result;
    if(r->op==TIKU_MEDIA_ENUMERATE&&(rc==0||rc==TIKU_MFS_NOENT)){
        r->names_epoch=q->names_epoch;r->rank=q->rank;
        if(!rc)*r->info=q->object;
    }
    if(!rc&&r->op==TIKU_MEDIA_OPEN_VERSION){
        if(q->rank!=r->rank||strcmp(q->object.name,r->name))return TIKU_MFS_STALE;
        for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(!s->readers[i].token){
            rc=tiku_mfs_query_open(s->fs,q,&r->id);
            if(!rc){s->readers[i].token=r->id;s->readers[i].client=r->client;*r->info=q->object;}
            return rc;
        }
        return TIKU_MFS_LIMIT;
    }
    return rc;
}
int tiku_media_async_step(tiku_media_async_t *a,tiku_media_store_t *s)
{
    if(!a||!s||!a->ops||!a->ops->acquire||!a->ops->read||!a->ops->poll||!a->ops->close)
        return TIKU_MFS_ARG;
    if(a->phase&&(a->store!=s||s->running!=2||!s->count||s->queue[0]!=a->request))return TIKU_MFS_BUSY;
    if(!a->phase){
        if(s->running)return TIKU_MFS_BUSY;
        if(!s->count)return 0;
        tiku_media_request_t *r=s->queue[0];
        if(r->op==TIKU_MEDIA_CLOSE){
            int slot=read_slot(s,r),rc=slot<0?TIKU_MFS_STALE:tiku_mfs_close_read(s->fs,r->id);
            if(!rc)s->readers[slot].token=0;
            finish(s,0,rc);return 1;
        }
        if(r->op!=TIKU_MEDIA_ENUMERATE&&r->op!=TIKU_MEDIA_OPEN_VERSION&&r->op!=TIKU_MEDIA_READ_AT)return 2;
        a->store=s;a->request=r;a->phase=1;s->running=2;
        int rc=begin(a);
        if(rc){finish(s,0,rc);s->running=0;retire(a);}
        return 1;
    }
    if(a->phase==1){
        int rc=a->ops->acquire(a->context,s->backend);
        if(rc==TIKU_MFS_BUSY)return 1;
        if(rc==1){a->opened=1;a->phase=2;return 1;}
        s->failed=s->fs->failed=1;a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;return 1;
    }
    if(a->phase==2||a->phase==4){
        int rc=a->ops->poll(a->context);
        if(rc==1||rc==TIKU_MFS_ARG)return 1;
        if(rc<0){a->opened=0;s->failed=s->fs->failed=1;a->result=rc;a->phase=5;return 1;}
        if(a->phase==4)(void)tiku_mfs_query_supply(&a->query,0);
        a->phase=3;return 1;
    }
    if(a->phase==3){
        if(a->query.waiting){
            int rc=a->ops->read(a->context,a->query.sector_lba,a->query.sector);
            if(rc==1)a->phase=4;
            else if(rc!=TIKU_MFS_BUSY){a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;}
        }else if(!tiku_mfs_query_step(s->fs,&a->query)){a->result=a->query.result;a->phase=5;}
        return 1;
    }
    if(a->opened){
        if(a->ops->close(a->context))return 1;
        a->opened=0;return 1;
    }
    int rc=result(a);finish(s,0,rc);s->running=0;retire(a);return 1;
}
int tiku_media_async_cancel(tiku_media_async_t *a,tiku_media_store_t *s,tiku_media_request_t *r)
{
    if(!a||!s||!r)return TIKU_MFS_ARG;
    unsigned i=0;while(i<s->count&&s->queue[i]!=r)i++;
    if(i==s->count)return r->done?0:TIKU_MFS_ARG;
    if(a->phase&&a->request==r){
        if(a->store!=s||s->running!=2||i)return TIKU_MFS_BUSY;
        if(a->opened&&a->ops->close(a->context))return TIKU_MFS_BUSY;
        s->running=0;retire(a);
    }else if(!i&&s->running)return TIKU_MFS_BUSY;
    finish(s,i,TIKU_MFS_STALE);return 0;
}
#endif
