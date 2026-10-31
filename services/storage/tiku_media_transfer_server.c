/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_transfer_server.c - Deferred storage requests on console and TCP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_server.h"
#include "tiku_media_transfer.h"
#include "tiku_media_card.h"
#include <services/console/tiku_console.h>
#include <shell/tiku_shell.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <string.h>
#if TIKU_KITS_NET_TCP_ENABLE
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#endif
#define CHANNEL 0xf7u
#define CLIENT 0x4d465402u
#define IDLE_TICKS (120u*TIKU_CLOCK_SECOND)
/** @brief Transfer memory remains caller-owned until the listener stops. */
typedef struct {
    tiku_media_transfer_t session;
    uint8_t pending[TIKU_MEDIA_TRANSFER_PACKET+4];
    uint8_t prefix[4],volume[16],key[32];
    uint32_t used,needed,queued,sending,sent;
} transfer_memory_t;
static transfer_memory_t *memory;
static tiku_arena_t arena;
static uint16_t listen_port;
static uint8_t active,lost,listening;
static uint8_t wire[TIKU_MEDIA_TRANSFER_PACKET];
static tiku_clock_time_t activity;
#if TIKU_KITS_NET_TCP_ENABLE
static tiku_kits_net_tcp_conn_t *connection;
#endif
/** @brief Validate backing before accessing transfer memory after an allocator reset. */
static int live(void)
{
    tiku_mem_stats_t stats;
    return memory&&tiku_arena_stats(&arena,&stats)==TIKU_MEM_OK&&stats.used_bytes==sizeof *memory;
}
/** @brief Release file handles by their static client identity after backing loss. */
static int release_handles(void)
{
    tiku_media_store_t *s=tiku_media_card_store();if(!s)return 0;
    tiku_media_request_t r={0};r.client=CLIENT;r.permissions=7;
    if(s->writer==CLIENT){r.op=TIKU_MEDIA_RELEASE;int rc=tiku_media_card_request(&r);if(rc)return rc;}
    for(unsigned i=0;i<TIKU_MFS_READERS;i++)if(s->readers[i].client==CLIENT&&s->readers[i].token){
        r.op=TIKU_MEDIA_CLOSE;r.id=s->readers[i].token;int rc=tiku_media_card_request(&r);if(rc)return rc;
    }
    return 0;
}
/** @brief Retain one decoded request for storage work outside the console callback. */
static void frame(void *ctx,uint8_t *data,size_t size)
{
    (void)ctx;if(!active||listen_port||!live()||memory->queued||memory->sending)return;
    memcpy(memory->pending,data,size);memory->queued=(uint32_t)size;
}
#if TIKU_KITS_NET_TCP_ENABLE
/** @brief Defer RX ring consumption to the shell pump. */
static void received(tiku_kits_net_tcp_conn_t *c,uint16_t available){(void)c;(void)available;}
/** @brief Admit one connection and defer storage cleanup after disconnect. */
static void event(tiku_kits_net_tcp_conn_t *c,uint8_t e)
{
    if(e==TIKU_KITS_NET_TCP_EVT_CONNECTED){
        if(connection||lost||!active||!live()){tiku_kits_net_tcp_abort(c);return;}
        connection=c;activity=tiku_clock_time();return;
    }
    if(connection==c){connection=NULL;lost=1;}
}
/** @brief Abort a stalled or malformed TCP stream without freeing the listener. */
static void drop(void)
{if(connection)tiku_kits_net_tcp_abort(connection);connection=NULL;lost=1;}
/** @brief Incrementally consume one length-prefixed request without retaining RX pointers. */
static void receive_packet(void)
{
    transfer_memory_t *m=memory;
    while(connection&&!m->queued&&!m->sending){
        if(m->used<4){
            unsigned got=tiku_kits_net_tcp_read(connection,m->prefix+m->used,(uint16_t)(4-m->used));
            if(!got)return;
            m->used+=got;if(m->used<4)return;
            m->needed=0;for(unsigned i=0;i<4;i++)m->needed|=(uint32_t)m->prefix[i]<<(8*i);
            if(m->needed<32||m->needed>TIKU_MEDIA_TRANSFER_PACKET){drop();return;}
        }
        unsigned at=m->used-4;
        unsigned got=tiku_kits_net_tcp_read(connection,m->pending+at,(uint16_t)(m->needed-at));
        if(!got)return;
        m->used+=got;
        if(m->used==m->needed+4){m->queued=m->needed;m->used=m->needed=0;}
    }
}
/** @brief Send one reply segment after the preceding bytes have been acknowledged. */
static void send_packet(void)
{
    transfer_memory_t *m=memory;
    if(!connection||!m->sending||connection->snd_nxt!=connection->snd_una)return;
    unsigned n=m->sending+4-m->sent;
    if(n>connection->snd_mss)n=connection->snd_mss;
    if(tiku_kits_net_tcp_send(connection,m->pending+m->sent,(uint16_t)n))return;
    m->sent+=n;if(m->sent==m->sending+4)m->sending=m->sent=0;
}
#endif
/** @brief Run one queued storage operation and bounded transport progress. */
static void pump(void)
{
    if(!active)return;
    tiku_watchdog_kick();
    if(!live()){(void)tiku_media_transfer_server_stop();return;}
    tiku_media_store_t *s=tiku_media_card_store();
    if(!s||memcmp(s->uuid,memory->volume,16)){(void)tiku_media_transfer_server_stop();return;}
    tiku_clock_time_t now=tiku_clock_time();
#if TIKU_KITS_NET_TCP_ENABLE
    if(listen_port){
        if(connection&&connection->state==TIKU_KITS_NET_TCP_STATE_CLOSE_WAIT)drop();
        if(connection&&(tiku_clock_time_t)(now-activity)>IDLE_TICKS)drop();
        if(lost){
            if(release_handles())return;
            tiku_media_transfer_init(&memory->session,s,CLIENT,memory->key);
            memory->queued=memory->used=memory->sending=memory->sent=0;lost=0;
        }
        receive_packet();
    }
#endif
    if(!listen_port&&memory->session.authenticated&&(tiku_clock_time_t)(now-activity)>IDLE_TICKS){
        if(release_handles())return;
        tiku_media_transfer_init(&memory->session,s,CLIENT,NULL);memory->queued=memory->sending=0;
    }
    if(memory->queued&&!memory->sending){
        memory->sending=(uint32_t)tiku_media_transfer_handle(&memory->session,memory->pending,memory->queued);
        memory->queued=memory->sent=0;activity=now;
        if(listen_port&&memory->sending){
            for(unsigned i=0;i<4;i++)memory->pending[i]=(uint8_t)(memory->sending>>(8*i));
            memcpy(memory->pending+4,memory->session.reply,memory->sending);
        }
    }
    if(!listen_port&&memory->sending){
        (void)tiku_console_send_frame(CHANNEL,memory->session.reply,memory->sending,NULL,0);
        tiku_console_flush();memory->sending=0;
    }
#if TIKU_KITS_NET_TCP_ENABLE
    if(listen_port)send_packet();
#endif
}
int tiku_media_transfer_server_stop(void)
{
    tiku_console_remove_channel(CHANNEL,0xff);tiku_shell_remove_pump(pump);
#if TIKU_KITS_NET_TCP_ENABLE
    if(listening)(void)tiku_kits_net_tcp_unlisten(listen_port);
    if(connection)tiku_kits_net_tcp_abort(connection);
    connection=NULL;
#endif
    active=0;listen_port=0;lost=0;listening=0;
    int rc=release_handles();if(rc)return rc;
    if(live()){
        memset(memory,0,sizeof *memory);
        if(tiku_arena_reset(&arena)||tiku_arena_destroy(&arena))return TIKU_MFS_BUSY;
    }
    memory=NULL;memset(&arena,0,sizeof arena);return 0;
}
int tiku_media_transfer_server_start(uint16_t port,const uint8_t *key)
{
    if(port){unsigned nonzero=0;if(!key)return TIKU_MFS_ARG;
        for(unsigned i=0;i<32;i++)nonzero|=key[i];
        if(!nonzero)return TIKU_MFS_ARG;}
#if !TIKU_KITS_NET_TCP_ENABLE
    if(port)return TIKU_MFS_ARG;
#endif
    int rc=tiku_media_transfer_server_stop();if(rc)return rc;
    tiku_media_store_t *s=tiku_media_card_store();if(!s)return TIKU_MFS_NOFS;
    tiku_mem_request_t opts={0};opts.allocation_class=TIKU_MEM_TRANSIENT;
    if(tiku_tier_arena_create_opts(&arena,TIKU_MEM_SRAM,sizeof *memory,92,&opts))return TIKU_MFS_SPACE;
    memory=tiku_arena_alloc(&arena,sizeof *memory);
    if(!memory){(void)tiku_arena_destroy(&arena);return TIKU_MFS_SPACE;}
    memset(memory,0,sizeof *memory);memcpy(memory->volume,s->uuid,16);
    if(port)memcpy(memory->key,key,32);
    tiku_media_transfer_init(&memory->session,s,CLIENT,port?key:NULL);
    listen_port=port;activity=tiku_clock_time();
    if(tiku_shell_add_pump(pump)){(void)tiku_media_transfer_server_stop();return TIKU_MFS_BUSY;}
#if TIKU_KITS_NET_TCP_ENABLE
    if(port){tiku_kits_net_tcp_init();
        if(tiku_kits_net_tcp_listen(port,received,event)){(void)tiku_media_transfer_server_stop();return TIKU_MFS_BUSY;}
        listening=1;}
#endif
    if(!port&&tiku_console_add_channel(CHANNEL,0xff,0,frame,NULL,wire,sizeof wire)){
        (void)tiku_media_transfer_server_stop();return TIKU_MFS_BUSY;}
    active=1;return 0;
}
