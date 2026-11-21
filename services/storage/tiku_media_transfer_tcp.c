/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_tcp.c - deferred TCP binding with retained connection cleanup.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_tcp.h"
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#include <string.h>
#if TIKU_KITS_NET_TCP_ENABLE && TIKU_KITS_NET_TCP_DEFERRED_ENABLE
/** @brief Callback ownership and bounded rejected-connection reservations. */
struct tiku_mt_tcp {
    int (*owner)(void *);
    int (*cancel)(void *,tiku_media_request_t *);
    void *context;
    tiku_mt_control_t *control;
    tiku_mt_listener_t *listener;
    tiku_kits_net_tcp_conn_t *accepted,*rejected[TIKU_KITS_NET_TCP_MAX_CONNS];
    uint32_t now;
    uint16_t port;
    unsigned released,removing,scan,turn;
    uint64_t control_storage[32];
    _Alignas(8) unsigned char listener_storage[];
};
static tiku_mt_tcp_t *tcp_binding;
/** @brief Validate the live binding and its serialized owner. */
static int tcp_allowed(tiku_mt_tcp_t *t)
{ return !t||t!=tcp_binding?TIKU_MFS_ARG:t->owner(t->context)?0:TIKU_MT_CONTROL_OWNER; }
/** @brief Delegate the serialized owner predicate to the bound service. */
static int tcp_owner(void *context)
{ tiku_mt_tcp_t *t=context;return t&&t==tcp_binding&&t->owner(t->context); }
/** @brief Quiesce an unaccepted connection and retain its table reservation. */
static void tcp_reject(tiku_mt_tcp_t *t,tiku_kits_net_tcp_conn_t *c)
{
    for(unsigned i=0;i<TIKU_KITS_NET_TCP_MAX_CONNS;++i)if(t->rejected[i]==c)return;
    for(unsigned i=0;i<TIKU_KITS_NET_TCP_MAX_CONNS;++i)if(!t->rejected[i]){
        if(tiku_kits_net_tcp_discard_step(c))t->rejected[i]=c;
        return;
    }
}
/** @brief Retire one rejected reservation by at most one queued segment. */
static int tcp_reject_step(tiku_mt_tcp_t *t)
{
    for(unsigned i=0;i<TIKU_KITS_NET_TCP_MAX_CONNS;++i)if(t->rejected[i]){
        if(!tiku_kits_net_tcp_discard_step(t->rejected[i]))t->rejected[i]=NULL;
        return 1;
    }
    return 0;
}
/** @brief Leave receive bytes in TCP's ring for bounded service reads. */
static void tcp_received(tiku_kits_net_tcp_conn_t *c,uint16_t bytes){(void)c;(void)bytes;}
/** @brief Reserve a disconnected accepted slot before the network caller frees it. */
static void tcp_event(tiku_kits_net_tcp_conn_t *c,uint8_t event)
{
    tiku_mt_tcp_t *t=tcp_binding;if(!t)return;
    if(event==TIKU_KITS_NET_TCP_EVT_CONNECTED){
        if(c==t->accepted&&!t->released)return;
        if(!tiku_mt_listener_connected(t->listener,c,t->now)){t->accepted=c;t->released=0;}
        else tcp_reject(t,c);
    }else if(c==t->accepted){
        t->released=!tiku_kits_net_tcp_discard_step(c);
        (void)tiku_mt_listener_closed(t->listener,c);
    }
}
/** @brief Attach a keyed service endpoint after its packet buffers are initialized. */
static int tcp_listen(void *context,uint16_t port)
{
    tiku_mt_tcp_t *t=context;
    int rc=tiku_kits_net_tcp_listen(port,tcp_received,tcp_event);
    if(rc)return rc==TIKU_KITS_NET_ERR_OVERFLOW?TIKU_MFS_BUSY:TIKU_MFS_ARG;
    t->port=port;t->removing=t->scan=0;return 0;
}
/** @brief Detach the endpoint and retire incomplete handshakes without affecting another listener. */
static int tcp_unlisten(void *context,uint16_t port)
{
    tiku_mt_tcp_t *t=context;
    if(!t->removing){
        if(tiku_kits_net_tcp_unlisten(port))return TIKU_MFS_IO;
        t->removing=1;return TIKU_MFS_BUSY;
    }
    if(tcp_reject_step(t))return TIKU_MFS_BUSY;
    if(t->scan<TIKU_KITS_NET_TCP_MAX_CONNS){
        tiku_kits_net_tcp_conn_t *c=tiku_kits_net_tcp_listener_connection(port,tcp_event,t->scan++);
        if(c&&(c!=t->accepted||t->released))tcp_reject(t,c);
        return TIKU_MFS_BUSY;
    }
    return 0;
}
/** @brief Consume bounded RX bytes without sending window-update acknowledgements. */
static int tcp_read(void *context,void *connection,void *out,size_t cap)
{
    tiku_mt_tcp_t *t=context;tiku_kits_net_tcp_conn_t *c=connection;
    if(t->released||c!=t->accepted||c->active!=1||c->state!=TIKU_KITS_NET_TCP_STATE_ESTABLISHED)return TIKU_MFS_IO;
    if(cap>TIKU_KITS_NET_TCP_DEFERRED_CHUNK)cap=TIKU_KITS_NET_TCP_DEFERRED_CHUNK;
    return tiku_kits_net_tcp_read_deferred(c,out,(uint16_t)cap);
}
/** @brief Copy one peer-window/MSS-limited segment without link output. */
static int tcp_send(void *context,void *connection,const void *data,size_t size)
{
    tiku_mt_tcp_t *t=context;tiku_kits_net_tcp_conn_t *c=connection;
    if(t->released||c!=t->accepted||c->active!=1||c->state!=TIKU_KITS_NET_TCP_STATE_ESTABLISHED)return TIKU_MFS_IO;
    if(size>TIKU_KITS_NET_TCP_DEFERRED_CHUNK)size=TIKU_KITS_NET_TCP_DEFERRED_CHUNK;
    if(size>TIKU_KITS_NET_TCP_MSS)size=TIKU_KITS_NET_TCP_MSS;
    if(size>c->snd_mss)size=c->snd_mss;
    if(size>c->snd_wnd)size=c->snd_wnd;
    if(!size)return TIKU_MFS_BUSY;
    int rc=tiku_kits_net_tcp_send_deferred(c,data,(uint16_t)size);
    return !rc?(int)size:rc==TIKU_KITS_NET_ERR_OVERFLOW?TIKU_MFS_BUSY:TIKU_MFS_IO;
}
/** @brief Retire the accepted reservation without accessing a slot released by link loss. */
static int tcp_retire(void *context,void *connection)
{
    tiku_mt_tcp_t *t=context;if(connection!=t->accepted)return TIKU_MFS_ARG;
    int rc=t->released?0:tiku_kits_net_tcp_discard_step(connection);
    if(!rc){t->accepted=NULL;t->released=0;}return rc;
}
/** @brief Forward cancellation to the existing media owners. */
static int tcp_cancel(void *context,tiku_media_request_t *request)
{ tiku_mt_tcp_t *t=context;return t->cancel(t->context,request); }
size_t tiku_mt_tcp_size(void){return sizeof(tiku_mt_tcp_t)+tiku_mt_listener_size();}
size_t tiku_mt_tcp_align(void){return _Alignof(tiku_mt_tcp_t);}
tiku_mt_tcp_t *tiku_mt_tcp_init(void *storage,size_t size,tiku_media_store_t *store,uint32_t client,
                               int (*owner)(void *),int (*cancel)(void *,tiku_media_request_t *),void *context)
{
    if(tcp_binding||!storage||size<tiku_mt_tcp_size()||(uintptr_t)storage%tiku_mt_tcp_align()||
       !owner||!cancel||!owner(context)||!store||!client||tiku_mt_control_size()>256||
       tiku_mt_control_align()>8||tiku_mt_listener_align()>8)return NULL;
    tiku_mt_tcp_t *t=storage;memset(t,0,sizeof *t);t->owner=owner;t->cancel=cancel;t->context=context;tcp_binding=t;
    tiku_mt_listener_ops_t ops={tcp_owner,tcp_listen,tcp_unlisten,tcp_read,tcp_send,tcp_retire,tcp_cancel};
    t->listener=tiku_mt_listener_init(t->listener_storage,size-sizeof *t,store,client,ops,t);
    tiku_mt_control_ops_t control={tiku_mt_listener_in_owner,tiku_mt_listener_phase};
    t->control=tiku_mt_control_init(t->control_storage,sizeof t->control_storage,control,t->listener);
    if(!t->listener||!t->control){tcp_binding=NULL;return NULL;}return t;
}
int tiku_mt_tcp_configure(tiku_mt_tcp_t *t,const tiku_mt_control_profile_t *profile,uint64_t *generation)
{
    int rc=tcp_allowed(t);if(rc)return rc;
    rc=tiku_mt_control_configure(t->control,profile,generation);if(!rc)t->port=profile->port;return rc;
}
int tiku_mt_tcp_submit(tiku_mt_tcp_t *t,tiku_mt_control_command_t command,uint64_t generation,uint64_t *token)
{ int rc=tcp_allowed(t);return rc?rc:tiku_mt_control_submit(t->control,command,generation,token); }
int tiku_mt_tcp_take(tiku_mt_tcp_t *t,uint64_t token,int *result)
{ int rc=tcp_allowed(t);return rc?rc:tiku_mt_control_take(t->control,token,result); }
int tiku_mt_tcp_observe(tiku_mt_tcp_t *t,tiku_mt_tcp_snapshot_t *out)
{
    int rc=tcp_allowed(t);if(rc)return rc;if(!out)return TIKU_MFS_ARG;
    tiku_mt_tcp_snapshot_t state={.port=t->port};rc=tiku_mt_control_observe(t->control,&state.control);if(rc)return rc;
    rc=tiku_mt_listener_observe(t->listener,&state.session);if(!rc)*out=state;return rc;
}
int tiku_mt_tcp_step(tiku_mt_tcp_t *t,uint32_t now_ms)
{
    int rc=tcp_allowed(t);if(rc)return rc;t->now=now_ms;
    tiku_mt_control_snapshot_t state;rc=tiku_mt_control_observe(t->control,&state);if(rc)return rc;
    if(state.state==TIKU_MT_CONTROL_STARTING||state.state==TIKU_MT_CONTROL_STOPPING)return tiku_mt_control_step(t->control);
    if((++t->turn&1u)&&tcp_reject_step(t))return 1;
    return tiku_mt_listener_step(t->listener,now_ms);
}
int tiku_mt_tcp_forget(tiku_mt_tcp_t *t)
{ int rc=tcp_allowed(t);if(rc)return rc;rc=tiku_mt_control_forget(t->control);if(!rc)t->port=0;return rc; }
int tiku_mt_tcp_unbind(tiku_mt_tcp_t *t)
{
    int rc=tcp_allowed(t);if(rc)return rc;
    tiku_mt_control_snapshot_t state;rc=tiku_mt_control_observe(t->control,&state);if(rc)return rc;
    if(state.state!=TIKU_MT_CONTROL_UNCONFIGURED||state.completion_slots_free!=2||t->accepted)return TIKU_MFS_BUSY;
    for(unsigned i=0;i<TIKU_KITS_NET_TCP_MAX_CONNS;++i)if(t->rejected[i])return TIKU_MFS_BUSY;
    tcp_binding=NULL;return 0;
}
#endif
