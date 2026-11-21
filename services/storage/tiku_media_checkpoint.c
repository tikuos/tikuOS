/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_checkpoint.c - retained queue ownership during metadata publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_checkpoint.h"
#if TIKU_MEDIA_ASYNC_ENABLE
void tiku_media_checkpoint_init(tiku_media_checkpoint_t *a,const tiku_media_write_ops_t *ops,void *context)
{ if(a)*a=(tiku_media_checkpoint_t){.ops=ops,.context=context}; }
/** @brief Complete a request after its transport has released borrowed memory. */
static void checkpoint_finish(tiku_media_checkpoint_t *a,tiku_media_store_t *s,unsigned i,int result)
{
    tiku_media_request_t *r=s->queue[i];
    --s->count;for(unsigned j=i;j<s->count;++j)s->queue[j]=s->queue[j+1];
    r->result=result;r->done=1;
    if(a->request==r){s->running=0;a->request=NULL;a->store=NULL;a->phase=a->opened=0;}
}
int tiku_media_checkpoint_step(tiku_media_checkpoint_t *a,tiku_media_store_t *s)
{
    if(!a||!s||!a->ops||!a->ops->acquire||!a->ops->write||!a->ops->sync||!a->ops->poll||!a->ops->close)
        return TIKU_MFS_ARG;
    if(a->phase&&(a->store!=s||s->running!=3||!s->count||s->queue[0]!=a->request))return TIKU_MFS_BUSY;
    if(!a->phase){
        if(s->running)return TIKU_MFS_BUSY;
        if(!s->count)return 0;
        if(s->queue[0]->op!=TIKU_MEDIA_CHECKPOINT)return 2;
        a->store=s;a->request=s->queue[0];a->phase=1;s->running=3;
        int rc=s->failed?TIKU_MFS_IO:tiku_mfs_checkpoint_begin(s->fs,&a->checkpoint);
        if(rc)checkpoint_finish(a,s,0,rc);
        return 1;
    }
    if(a->phase==1){
        int rc=a->ops->acquire(a->context,s->backend);
        if(rc==TIKU_MFS_BUSY)return 1;
        if(rc==1){a->opened=1;a->phase=2;return 1;}
        a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;return 1;
    }
    if(a->phase==2||a->phase==4){
        int rc=a->ops->poll(a->context);
        if(rc==1||rc==TIKU_MFS_ARG)return 1;
        if(rc<0){
            a->opened=0;a->result=rc;a->phase=5;
            s->failed=s->fs->failed=1;return 1;
        }
        if(a->phase==4)(void)tiku_mfs_checkpoint_supply(&a->checkpoint,0);
        a->phase=3;return 1;
    }
    if(a->phase==3){
        if(a->checkpoint.waiting){
            int rc=a->checkpoint.waiting==TIKU_MFS_CHECKPOINT_WRITE?
                a->ops->write(a->context,a->checkpoint.sector_lba,a->checkpoint.sector):a->ops->sync(a->context);
            if(rc==1)a->phase=4;
            else if(rc!=TIKU_MFS_BUSY){
                a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;
                s->failed=s->fs->failed=1;
            }
        }else if(!tiku_mfs_checkpoint_step(s->fs,&a->checkpoint)){
            a->result=a->checkpoint.result;a->phase=5;
        }
        return 1;
    }
    if(a->opened){if(a->ops->close(a->context))return 1;a->opened=0;return 1;}
    checkpoint_finish(a,s,0,a->result);return 1;
}
int tiku_media_checkpoint_cancel(tiku_media_checkpoint_t *a,tiku_media_store_t *s,tiku_media_request_t *r)
{
    if(!a||!s||!r)return TIKU_MFS_ARG;
    unsigned i=0;while(i<s->count&&s->queue[i]!=r)++i;
    if(i==s->count)return r->done?0:TIKU_MFS_ARG;
    if(a->request==r){
        if(a->store!=s||s->running!=3||i)return TIKU_MFS_BUSY;
        if(a->opened&&a->ops->close(a->context))return TIKU_MFS_BUSY;
        if(a->checkpoint.waiting)(void)tiku_mfs_checkpoint_supply(&a->checkpoint,TIKU_MFS_IO);
        (void)tiku_mfs_checkpoint_cancel(s->fs,&a->checkpoint);
        if(s->fs->failed)s->failed=1;
    }else if(!i&&s->running)return TIKU_MFS_BUSY;
    checkpoint_finish(a,s,i,TIKU_MFS_STALE);return 0;
}
#endif
