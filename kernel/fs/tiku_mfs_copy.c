/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mfs_copy.c - Incremental verified object replacement for compaction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mfs_copy.h"
#include <string.h>
int tiku_mfs_copy_begin(tiku_mfs_t *fs,tiku_mfs_copy_t *copy,const char *name)
{
    if(!fs||!copy)return TIKU_MFS_ARG;
    if(fs->writing)return TIKU_MFS_BUSY;
    tiku_mfs_object_t o;int rc=tiku_mfs_stat(fs,name,&o);if(rc)return rc;
    if(!o.complete)return TIKU_MFS_BUSY;
    memset(copy,0,sizeof *copy);
    if((rc=tiku_mfs_open_read(fs,name,&copy->reader)))return rc;
    rc=tiku_mfs_begin(fs,name,TIKU_MFS_ATOMIC,o.length,o.digest,&copy->id);
    if(rc){(void)tiku_mfs_close_read(fs,copy->reader);return rc;}
    copy->length=o.length;copy->active=1;return 0;
}
int tiku_mfs_copy_step(tiku_mfs_t *fs,tiku_mfs_copy_t *copy,void *buffer,uint32_t capacity)
{
    if(!fs||!copy||!copy->active||!buffer||!capacity||capacity>TIKU_MFS_CHUNK)return TIKU_MFS_ARG;
    if(!fs->writing||fs->draft.id!=copy->id)return TIKU_MFS_STALE;
    if(copy->offset<copy->length){
        int n=tiku_mfs_read_at(fs,copy->reader,copy->offset,buffer,capacity);
        if(n<=0)return n<0?n:TIKU_MFS_CORRUPT;
        int rc=tiku_mfs_append(fs,buffer,(uint32_t)n);if(rc)return rc;
        if((rc=tiku_mfs_sync(fs)))return rc;
        copy->offset+=(unsigned)n;
    }
    if(copy->offset<copy->length)return 1;
    int rc=tiku_mfs_finalize(fs);if(rc)return rc;
    rc=tiku_mfs_close_read(fs,copy->reader);if(!rc)copy->active=0;return rc;
}
int tiku_mfs_copy_abort(tiku_mfs_t *fs,tiku_mfs_copy_t *copy)
{
    if(!fs||!copy||!copy->active)return TIKU_MFS_ARG;
    if(!fs->writing||fs->draft.id!=copy->id)return TIKU_MFS_STALE;
    int rc=tiku_mfs_release(fs);if(rc)return rc;
    rc=tiku_mfs_close_read(fs,copy->reader);if(rc)return rc;
    copy->active=0;return tiku_mfs_delete(fs,copy->id);
}
