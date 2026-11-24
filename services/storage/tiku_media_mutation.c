/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_mutation.c - bounded queue progression for file mutations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_mutation.h"
#if TIKU_MEDIA_ASYNC_ENABLE
void tiku_media_mutation_init(tiku_media_mutation_t *a,tiku_mfs_mutation_t *m,const tiku_media_mutation_ops_t *ops,void *context)
{ if(a)*a=(tiku_media_mutation_t){.mutation=m,.ops=ops,.context=context}; }
/** @brief Retire core state after the transport has released its sector lease. */
static void mutation_retire(tiku_media_mutation_t *a,tiku_media_store_t *s)
{
    if(!a->begun)return;
    uint64_t lba;void *p;
    if(tiku_mfs_mutation_io(a->mutation,&lba,&p))(void)tiku_mfs_mutation_supply(a->mutation,TIKU_MFS_IO);
    (void)tiku_mfs_mutation_cancel(s->fs,a->mutation);
    if(s->fs->failed)s->failed=1;
}
/** @brief Complete one request and release queue serialization after transport retirement. */
static void mutation_finish(tiku_media_mutation_t *a,tiku_media_store_t *s,unsigned i,int result)
{
    tiku_media_request_t *r=s->queue[i];
    unsigned published=a->request==r&&a->begun&&!tiku_mfs_mutation_result(a->mutation);
    if(published&&(r->op==TIKU_MEDIA_BEGIN||r->op==TIKU_MEDIA_RESUME)){
        r->id=tiku_mfs_mutation_id(a->mutation);s->writer=r->client;
        if(r->info)*r->info=s->fs->draft;
    }
    if(published&&r->op==TIKU_MEDIA_FINALIZE)s->writer=0;
    if(!result&&(r->op==TIKU_MEDIA_REPLACE
#if TIKU_MEDIA_DIRECTORIES_ENABLE
       ||r->op==TIKU_MEDIA_MKDIR||r->op==TIKU_MEDIA_TREE_COPY
#endif
       ))r->id=tiku_mfs_mutation_id(a->mutation);
    if(a->request==r){
        mutation_retire(a,s);s->running=0;a->request=NULL;a->store=NULL;a->phase=a->opened=a->begun=0;
    }
    --s->count;for(unsigned j=i;j<s->count;++j)s->queue[j]=s->queue[j+1];
    r->result=result;r->done=1;
}
int tiku_media_mutation_step(tiku_media_mutation_t *a,tiku_media_store_t *s)
{
    if(!a||!s||!a->mutation||!a->ops||!a->ops->acquire||!a->ops->read||!a->ops->write||
       !a->ops->sync||!a->ops->poll||!a->ops->close)return TIKU_MFS_ARG;
    if(a->phase&&(a->store!=s||s->running!=4||!s->count||s->queue[0]!=a->request))return TIKU_MFS_BUSY;
    if(!a->phase){
        if(s->running)return TIKU_MFS_BUSY;
        if(!s->count)return 0;
        tiku_media_request_t *r=s->queue[0];
        if(r->op==TIKU_MEDIA_RELEASE){
            int rc=s->writer!=r->client||s->copy.active?TIKU_MFS_BUSY:tiku_mfs_release(s->fs);
            if(!rc)s->writer=0;
            mutation_finish(a,s,0,rc);return 1;
        }
        unsigned streaming=r->op>=TIKU_MEDIA_BEGIN&&r->op<=TIKU_MEDIA_FINALIZE;
        if(!streaming&&r->op!=TIKU_MEDIA_RENAME&&r->op!=TIKU_MEDIA_DELETE_VERSION&&r->op!=TIKU_MEDIA_REPLACE&&
           r->op!=TIKU_MEDIA_DISCARD&&r->op!=TIKU_MEDIA_RENAME_OVER
#if TIKU_MEDIA_DIRECTORIES_ENABLE
            &&r->op!=TIKU_MEDIA_MKDIR&&r->op!=TIKU_MEDIA_TREE_RENAME&&r->op!=TIKU_MEDIA_TREE_DELETE&&r->op!=TIKU_MEDIA_DIRECTORY_UPGRADE&&r->op!=TIKU_MEDIA_TREE_COPY
#endif
            )return 2;
        a->store=s;a->request=r;a->phase=1;s->running=4;
        int rc=s->failed?TIKU_MFS_IO:(s->collector||s->copy.active||
            (streaming&&r->op>=TIKU_MEDIA_APPEND?s->writer!=r->client:s->writer!=0))?TIKU_MFS_BUSY:
            r->op==TIKU_MEDIA_RENAME&&!r->buffer?TIKU_MFS_ARG:
            r->names_epoch&&r->names_epoch!=s->fs->names_epoch?TIKU_MFS_STALE:
#if TIKU_MEDIA_DIRECTORIES_ENABLE
            r->op==TIKU_MEDIA_TREE_COPY?(!r->names_epoch?TIKU_MFS_ARG:
                tiku_mfs_mutation_tree_copy_begin(s->fs,a->mutation,r->id,r->name,r->destination,r->buffer,r->length)):
            r->op==TIKU_MEDIA_DIRECTORY_UPGRADE?(!r->names_epoch?TIKU_MFS_ARG:tiku_mfs_mutation_upgrade_begin(s->fs,a->mutation)):
            (r->op==TIKU_MEDIA_TREE_RENAME||r->op==TIKU_MEDIA_TREE_DELETE)?
                ((!r->names_epoch||(r->op==TIKU_MEDIA_TREE_RENAME&&!r->buffer))?TIKU_MFS_ARG:
                 tiku_mfs_mutation_tree_begin(s->fs,a->mutation,r->id,r->name,r->op==TIKU_MEDIA_TREE_RENAME?r->buffer:NULL)):
            r->op==TIKU_MEDIA_MKDIR?tiku_mfs_mutation_mkdir_begin(s->fs,a->mutation,r->name):
#endif
            r->op==TIKU_MEDIA_BEGIN?tiku_mfs_mutation_upload_begin(s->fs,a->mutation,r->id,r->name,r->policy,r->offset,r->digest):
            r->op==TIKU_MEDIA_RESUME?tiku_mfs_mutation_resume_begin(s->fs,a->mutation,r->id):
            r->op==TIKU_MEDIA_APPEND?tiku_mfs_mutation_append_begin(s->fs,a->mutation,r->buffer,r->length):
            (r->op==TIKU_MEDIA_SYNC||r->op==TIKU_MEDIA_FINALIZE)?tiku_mfs_mutation_flush_begin(s->fs,a->mutation,r->op==TIKU_MEDIA_FINALIZE):
            r->op==TIKU_MEDIA_REPLACE?tiku_mfs_mutation_replace_begin(s->fs,a->mutation,r->id,r->name,r->buffer,r->length):
            r->op==TIKU_MEDIA_DISCARD?tiku_mfs_mutation_discard_begin(s->fs,a->mutation,r->id,r->name):
            r->op==TIKU_MEDIA_RENAME_OVER?(!r->buffer?TIKU_MFS_ARG:
                tiku_mfs_mutation_rename_over_begin(s->fs,a->mutation,r->id,r->name,r->buffer,r->offset)):
            tiku_mfs_mutation_begin(s->fs,a->mutation,r->id,r->name,r->op==TIKU_MEDIA_RENAME?r->buffer:NULL);
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        r->prepared=r->total=0;
#endif
        a->begun=rc==0;if(rc)mutation_finish(a,s,0,rc);return 1;
    }
    if(a->phase==1){
        int rc=a->ops->acquire(a->context,s->backend);
        if(rc==TIKU_MFS_BUSY)return 1;
        if(rc==1){a->opened=1;a->phase=2;return 1;}
        a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;return 1;
    }
    if(a->phase==2||a->phase==4){
        int rc=a->ops->poll(a->context);if(rc==1||rc==TIKU_MFS_ARG)return 1;
        if(rc<0){a->opened=0;a->result=rc;a->phase=5;s->failed=s->fs->failed=1;return 1;}
        if(a->phase==4)(void)tiku_mfs_mutation_supply(a->mutation,0);
        a->phase=3;return 1;
    }
    if(a->phase==3){
        uint64_t lba;void *sector;int kind=tiku_mfs_mutation_io(a->mutation,&lba,&sector);
        if(kind){
            int rc=kind==TIKU_MFS_MUTATION_READ?a->ops->read(a->context,lba,sector):
                kind==TIKU_MFS_MUTATION_WRITE?a->ops->write(a->context,lba,sector):a->ops->sync(a->context);
            if(rc==1)a->phase=4;
            else if(rc!=TIKU_MFS_BUSY){a->result=rc<0?rc:TIKU_MFS_IO;a->phase=5;s->failed=s->fs->failed=1;}
        }else if(!tiku_mfs_mutation_step(s->fs,a->mutation)){
            a->result=tiku_mfs_mutation_result(a->mutation);a->phase=5;
        }
#if TIKU_MEDIA_DIRECTORIES_ENABLE
        if(a->request->op==TIKU_MEDIA_TREE_COPY)
            (void)tiku_mfs_mutation_progress(a->mutation,&a->request->prepared,&a->request->total);
#endif
        return 1;
    }
    if(a->opened){if(a->ops->close(a->context))return 1;a->opened=0;return 1;}
    mutation_finish(a,s,0,a->result);return 1;
}
int tiku_media_mutation_cancel(tiku_media_mutation_t *a,tiku_media_store_t *s,tiku_media_request_t *r)
{
    if(!a||!s||!r)return TIKU_MFS_ARG;
    unsigned i=0;while(i<s->count&&s->queue[i]!=r)++i;
    if(i==s->count)return r->done?0:TIKU_MFS_ARG;
    if(a->request==r){
        if(a->store!=s||s->running!=4||i)return TIKU_MFS_BUSY;
        if(a->opened&&a->ops->close(a->context))return TIKU_MFS_BUSY;
    }else if(!i&&s->running)return TIKU_MFS_BUSY;
    mutation_finish(a,s,i,TIKU_MFS_STALE);return 0;
}
#endif
