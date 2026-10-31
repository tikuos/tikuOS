/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_vfs.c - Read-only VFS adapter over queued native media storage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_vfs.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
static tiku_media_store_t *store;
static uint8_t serving;
/** @brief Map native filesystem failures to the VFS vocabulary. */
static int status(int rc)
{
    if(rc>=0)return rc;
    switch(rc){
    case TIKU_MFS_ARG:return TIKU_VFS_EINVAL;
    case TIKU_MFS_NOENT:return TIKU_VFS_ENOENT;
    case TIKU_MFS_STALE:case TIKU_MFS_BUSY:return TIKU_VFS_EBUSY;
    case TIKU_MFS_NOFS:return TIKU_VFS_ENOTSUP;
    case TIKU_MFS_LIMIT:case TIKU_MFS_SPACE:return TIKU_VFS_E2BIG;
    case TIKU_MFS_CORRUPT:case TIKU_MFS_DIGEST:return TIKU_VFS_ECORRUPT;
    default:return TIKU_VFS_EIO;
    }
}
/** @brief Run a VFS request with an empty queue and release its borrowed arguments. */
static int request(tiku_media_request_t *r)
{
    if(!store||!store->fs->mounted)return TIKU_VFS_ENOTSUP;
    if(store->count||store->running)return TIKU_VFS_EBUSY;
    r->client=UINT32_MAX;r->permissions=TIKU_MEDIA_READ;
    int rc=tiku_media_store_submit(store,r);if(rc)return status(rc);
    rc=tiku_media_store_step(store);
    if(rc==TIKU_MFS_BUSY){store->count=0;return TIKU_VFS_EBUSY;}
    return status(r->result);
}
/** @brief Serve a bounded range; files larger than the VFS ABI are refused. */
static int read_at(const char *name,uint32_t offset,char *buf,size_t max,uint32_t *total)
{
    if(serving)return TIKU_VFS_EBUSY;
    serving=1;tiku_mfs_object_t o;tiku_media_request_t r={0};
    r.op=TIKU_MEDIA_STAT;r.name=name;r.info=&o;int rc=request(&r);
    if(!rc&&o.length>UINT32_MAX)rc=TIKU_VFS_E2BIG;
    if(!rc){
        if(total)*total=(uint32_t)o.length;
        r.op=TIKU_MEDIA_OPEN;rc=request(&r);
        if(!rc){
            r.op=TIKU_MEDIA_READ_AT;r.offset=offset;r.buffer=buf;
            r.length=(uint32_t)(max>TIKU_MFS_CHUNK?TIKU_MFS_CHUNK:max);rc=request(&r);
            r.op=TIKU_MEDIA_CLOSE;int close=request(&r);if(close<0)rc=close;
        }
    }
    serving=0;return rc;
}
/** @brief Serve legacy reads whose full length fits the return-value ABI. */
static int read_file(const char *name,char *buf,size_t max)
{
    uint32_t total=0;int rc=read_at(name,0,buf,max,&total);if(rc<0)return rc;
    if(total>INT_MAX||(total>(uint32_t)rc&&max>(size_t)rc))return TIKU_VFS_E2BIG;
    return (int)total;
}
/** @brief Resolve a visible file or prefix directory through indexed service requests. */
static int is_dir(const char *name)
{
    if(serving)return TIKU_VFS_EBUSY;
    serving=1;tiku_mfs_object_t o;tiku_media_request_t r={0};
    r.op=TIKU_MEDIA_STAT;r.name=name;r.info=&o;int rc=request(&r);
    if(rc==TIKU_VFS_ENOENT){
        size_t n=strlen(name);char prefix[TIKU_MFS_NAME];
        if(n+1<sizeof prefix){
            memcpy(prefix,name,n);prefix[n]='/';prefix[n+1]=0;
            r.op=TIKU_MEDIA_SEEK;r.name=prefix;rc=request(&r);
            if(!rc){r.op=TIKU_MEDIA_LIST;rc=request(&r);
                if(!rc)rc=!strncmp(prefix,o.name,n+1)?1:TIKU_VFS_ENOENT;}
        }
    }
    serving=0;return rc;
}
/** @brief Stream a prefix's sorted child names with one-name deduplication storage. */
static int list(const char *prefix,tiku_vfs_dyn_list_cb cb,void *ctx)
{
    if(serving)return TIKU_VFS_EBUSY;
    serving=1;tiku_mfs_object_t o;tiku_media_request_t r={0};
    r.op=TIKU_MEDIA_SEEK;r.name=prefix;r.info=&o;int rc=request(&r);
    char previous[TIKU_MFS_NAME]={0};size_t n=strlen(prefix);
    while(!rc){
        r.op=TIKU_MEDIA_LIST;rc=request(&r);
        if(rc==TIKU_VFS_ENOENT){rc=0;break;}
        if(rc)break;
        if(strncmp(prefix,o.name,n))break;
        const char *child=o.name+n;char name[TIKU_MFS_NAME];size_t k=0;
        while(child[k]&&child[k]!='/')k++;
        if(child[k]=='/')k++;
        if(!k||k>=sizeof name)continue;
        memcpy(name,child,k);name[k]=0;
        if(strcmp(previous,name)){memcpy(previous,name,k+1);cb(name,ctx);}
    }
    serving=0;return rc;
}
/** @brief Render cached mount and scheduling status without reading the medium. */
static int observation(char *buf,size_t max)
{
    return snprintf(buf,max,"mounted=%u failed=%u queued=%u writer=%lu collecting=%u\n",
        store?store->fs->mounted:0,store?store->failed||store->fs->failed:0,
        store?store->count:0,(unsigned long)(store?store->writer:0),store?store->fs->collecting:0);
}
static const tiku_vfs_dynops_t ops={.read=read_file,.read_at=read_at,.is_dir=is_dir,
    .list_checked=list,.read_only=1};
static const tiku_vfs_node_t files={.name="media",.type=TIKU_VFS_DIR,.dyn=&ops};
static const tiku_vfs_node_t observed={.name="media",.type=TIKU_VFS_FILE,.read=observation};
int tiku_media_vfs_attach(tiku_media_store_t *s)
{
    if(serving||(store&&(store->running||store->count)))return TIKU_MFS_BUSY;
    store=s;return 0;
}
const tiku_vfs_node_t *tiku_media_vfs_files(void){return &files;}
const tiku_vfs_node_t *tiku_media_vfs_status(void){return &observed;}
