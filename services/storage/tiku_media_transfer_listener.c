/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_listener.c - stream framing, command phases and session recovery.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_listener.h"
#include <string.h>

/** @brief Stream progression retains request input until packet completion. */
enum { LS_PREFIX,LS_BODY,LS_PACKET,LS_REPLY,LS_SEND };
/** @brief Storage erased before session reuse and after handle retirement. */
typedef struct {
    uint64_t packet[128];
    tiku_media_transfer_t session;
    uint8_t wire[4+TIKU_MEDIA_TRANSFER_PACKET];
} listener_work_t;
/** @brief Fixed metadata survives the incremental session erasure. */
struct tiku_mt_listener {
    tiku_mt_listener_ops_t ops;
    void *context,*connection;
    tiku_media_store_t *store;
    const tiku_mt_control_profile_t *profile;
    tiku_media_transfer_async_t *packet;
    uint32_t client,activity;
    size_t preparing,wiping,used,needed,copied,sent;
    unsigned attached,ready,lost,recovery,stream,running;
    int error;
    listener_work_t work;
};
/** @brief Validate service ownership before touching state. */
static int listener_allowed(tiku_mt_listener_t *l)
{ return !l?TIKU_MFS_ARG:l->ops.in_owner(l->context)?0:TIKU_MT_CONTROL_OWNER; }
/** @brief Reset framing after the preceding response has been copied to transport. */
static void listener_frame_reset(tiku_mt_listener_t *l)
{ l->stream=LS_PREFIX;l->used=l->copied=l->sent=0;l->needed=4; }
/** @brief Start cleanup without dropping a connection or a pending request. */
static int listener_recover(tiku_mt_listener_t *l,int error)
{
    l->ready=0;l->error=error;l->recovery=TIKU_MT_CONTROL_QUIESCE+1;
    return TIKU_MT_CONTROL_PENDING;
}
/** @brief Execute one lifecycle phase with retained preparation and erasure cursors. */
static int listener_phase(tiku_mt_listener_t *l,tiku_mt_control_phase_t phase,
                          const tiku_mt_control_profile_t *profile)
{
    int rc;
    switch(phase){
    case TIKU_MT_CONTROL_PREPARE:
        l->profile=profile;
        if(!l->store->fs||l->store->failed)return TIKU_MFS_NOFS;
        if(memcmp(l->store->uuid,profile->volume,16))return TIKU_MFS_STALE;
        if(l->preparing<sizeof l->work){
            (void)tiku_mt_control_wipe(&l->work,sizeof l->work,&l->preparing);
            return TIKU_MT_CONTROL_PENDING;
        }
        if(!l->packet){
            l->packet=tiku_media_transfer_async_init(l->work.packet,sizeof l->work.packet,
                &l->work.session,l->store,l->client,profile->key);
            if(!l->packet)return TIKU_MFS_LIMIT;
            listener_frame_reset(l);l->wiping=0;l->lost=0;l->error=0;
        }
        return 0;
    case TIKU_MT_CONTROL_ATTACH:
        if(!l->packet||l->store->failed||memcmp(l->store->uuid,profile->volume,16))return TIKU_MFS_STALE;
        if(!l->attached){rc=l->ops.listen(l->context,profile->port);if(rc)return rc;l->attached=1;}
        l->ready=l->running=1;return 0;
    case TIKU_MT_CONTROL_QUIESCE:
        l->ready=0;
        if(l->attached){
            rc=l->ops.unlisten(l->context,profile->port);if(rc)return rc;
            l->attached=0;return TIKU_MT_CONTROL_PENDING;
        }
        if(l->packet){
            tiku_media_request_t *r=tiku_media_transfer_async_request(l->packet);
            if(r&&!r->done){rc=l->ops.cancel(l->context,r);return rc?rc:TIKU_MT_CONTROL_PENDING;}
            return tiku_media_transfer_async_cancel(l->packet);
        }
        return 0;
    case TIKU_MT_CONTROL_DETACH:
        if(!l->connection)return 0;
        rc=l->ops.retire(l->context,l->connection);
        if(rc)return rc;
        l->connection=NULL;return 0;
    case TIKU_MT_CONTROL_RELEASE:
        return tiku_mt_control_release_one(l->store,l->client);
    case TIKU_MT_CONTROL_WIPE:
        rc=tiku_mt_control_wipe(&l->work,sizeof l->work,&l->wiping);
        if(!rc){l->packet=NULL;l->preparing=0;l->lost=0;listener_frame_reset(l);}
        return rc;
    }
    return TIKU_MFS_ARG;
}
size_t tiku_mt_listener_size(void){return sizeof(tiku_mt_listener_t);}
size_t tiku_mt_listener_align(void){return _Alignof(tiku_mt_listener_t);}
tiku_mt_listener_t *tiku_mt_listener_init(void *storage,size_t size,tiku_media_store_t *store,
                                         uint32_t client,tiku_mt_listener_ops_t ops,void *context)
{
    if(!storage||size<sizeof(tiku_mt_listener_t)||(uintptr_t)storage%_Alignof(tiku_mt_listener_t)||
       !store||!client||!ops.in_owner||!ops.listen||!ops.unlisten||!ops.read||!ops.send||
       !ops.retire||!ops.cancel||!ops.in_owner(context))return NULL;
    tiku_mt_listener_t *l=storage;memset(l,0,offsetof(tiku_mt_listener_t,work));
    l->ops=ops;l->context=context;l->store=store;l->client=client;return l;
}
int tiku_mt_listener_in_owner(void *context)
{ return !listener_allowed(context); }
int tiku_mt_listener_phase(void *context,tiku_mt_control_phase_t phase,const tiku_mt_control_profile_t *profile)
{
    tiku_mt_listener_t *l=context;int rc=listener_allowed(l);if(rc)return rc;
    if(!profile)return TIKU_MFS_ARG;
    if(phase==TIKU_MT_CONTROL_QUIESCE){l->recovery=0;l->running=0;}
    rc=listener_phase(l,phase,profile);
    if(!rc&&phase==TIKU_MT_CONTROL_WIPE)l->profile=NULL;
    return rc;
}
int tiku_mt_listener_connected(tiku_mt_listener_t *l,void *connection,uint32_t now_ms)
{
    int rc=listener_allowed(l);if(rc)return rc;
    if(!connection)return TIKU_MFS_ARG;
    if(!l->ready||l->connection)return TIKU_MFS_BUSY;
    l->connection=connection;l->activity=now_ms;return 0;
}
int tiku_mt_listener_closed(tiku_mt_listener_t *l,void *connection)
{
    int rc=listener_allowed(l);if(rc)return rc;
    if(!connection||connection!=l->connection)return TIKU_MFS_STALE;
    l->lost=1;l->ready=0;return 0;
}
int tiku_mt_listener_step(tiku_mt_listener_t *l,uint32_t now_ms)
{
    int rc=listener_allowed(l);if(rc)return rc;
    if(!l->running)return 0;
    if(l->recovery){
        tiku_mt_control_phase_t phase=(tiku_mt_control_phase_t)(l->recovery-1);
        rc=listener_phase(l,phase,l->profile);
        if(rc==TIKU_MFS_BUSY||rc==TIKU_MT_CONTROL_PENDING)return TIKU_MT_CONTROL_PENDING;
        if(rc){l->error=rc;l->ready=0;l->running=0;l->recovery=0;return rc;}
        l->recovery=phase==TIKU_MT_CONTROL_WIPE?TIKU_MT_CONTROL_PREPARE+1:
            phase==TIKU_MT_CONTROL_PREPARE?TIKU_MT_CONTROL_ATTACH+1:
            phase==TIKU_MT_CONTROL_ATTACH?0:l->recovery+1;
        return TIKU_MT_CONTROL_PENDING;
    }
    if(l->lost&&l->profile)return listener_recover(l,TIKU_MFS_IO);
    if(!l->ready)return 0;
    if(!l->store->fs||l->store->failed||memcmp(l->store->uuid,l->profile->volume,16))
        return listener_recover(l,TIKU_MFS_STALE);
    if(!l->connection)return 0;
    if((uint32_t)(now_ms-l->activity)>=120000u)return listener_recover(l,TIKU_MFS_IO);
    if(l->stream==LS_PACKET){
        if(tiku_media_transfer_async_step(l->packet))return TIKU_MT_CONTROL_PENDING;
        const uint8_t *reply;rc=tiku_media_transfer_async_result(l->packet,&reply);
        if(rc<=0)return listener_recover(l,rc?rc:TIKU_MFS_ARG);
        l->needed=(size_t)rc+4;l->copied=0;l->stream=LS_REPLY;
        for(unsigned i=0;i<4;++i)l->work.wire[i]=(uint8_t)((unsigned)rc>>(8*i));
        return TIKU_MT_CONTROL_PENDING;
    }
    if(l->stream==LS_REPLY){
        const uint8_t *reply;rc=tiku_media_transfer_async_result(l->packet,&reply);
        if(rc<0)return listener_recover(l,rc);
        size_t n=(size_t)rc-l->copied;if(n>512)n=512;
        memcpy(l->work.wire+4+l->copied,reply+l->copied,n);l->copied+=n;
        if(l->copied==(size_t)rc){l->sent=0;l->stream=LS_SEND;}
        return TIKU_MT_CONTROL_PENDING;
    }
    if(l->stream==LS_SEND){
        size_t n=l->needed-l->sent;if(n>512)n=512;
        rc=l->ops.send(l->context,l->connection,l->work.wire+l->sent,n);
        if(!rc||rc==TIKU_MFS_BUSY)return TIKU_MT_CONTROL_PENDING;
        if(rc<0||(size_t)rc>n)return listener_recover(l,rc<0?rc:TIKU_MFS_IO);
        l->activity=now_ms;l->sent+=(size_t)rc;
        if(l->sent==l->needed)listener_frame_reset(l);
        return TIKU_MT_CONTROL_PENDING;
    }
    size_t n=l->needed-l->used;if(n>512)n=512;
    rc=l->ops.read(l->context,l->connection,l->work.wire+l->used,n);
    if(!rc||rc==TIKU_MFS_BUSY)return 0;
    if(rc<0||(size_t)rc>n)return listener_recover(l,rc<0?rc:TIKU_MFS_IO);
    l->activity=now_ms;l->used+=(size_t)rc;
    if(l->used!=l->needed)return TIKU_MT_CONTROL_PENDING;
    if(l->stream==LS_PREFIX){
        uint32_t length=0;for(unsigned i=0;i<4;++i)length|=(uint32_t)l->work.wire[i]<<(8*i);
        if(length<32||length>TIKU_MEDIA_TRANSFER_PACKET)return listener_recover(l,TIKU_MFS_ARG);
        l->needed=(size_t)length+4;l->stream=LS_BODY;
    }else{
        rc=tiku_media_transfer_async_submit(l->packet,l->work.wire+4,l->used-4);
        if(rc)return listener_recover(l,rc);
        l->stream=LS_PACKET;
    }
    return TIKU_MT_CONTROL_PENDING;
}
int tiku_mt_listener_observe(tiku_mt_listener_t *l,tiku_mt_listener_snapshot_t *out)
{
    int rc=listener_allowed(l);if(rc)return rc;if(!out)return TIKU_MFS_ARG;
    *out=(tiku_mt_listener_snapshot_t){.ready=l->ready,.connected=l->connection!=NULL,
        .recovering=l->recovery!=0,.error=l->error};
    if(l->packet){out->accepted=l->work.session.accepted;out->committed=l->work.session.committed;
        out->upload=l->work.session.upload;out->expected=l->work.session.expected;
        out->read_offset=l->work.session.read_offset;out->read_length=l->work.session.read_length;
        out->reading=l->work.session.reader!=0;out->fault=l->work.session.fault;}
    return 0;
}
