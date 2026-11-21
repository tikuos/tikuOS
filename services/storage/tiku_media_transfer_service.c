/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_media_transfer_service.c - independent TCP command and card queue scheduling.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_media_transfer_service.h"
#if TIKU_MEDIA_TRANSFER_SERVICE_ENABLE
#include "tiku_media_card.h"
#include "tiku_media_transfer_server.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/threads/tiku_thread.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/process/tiku_process.h>
#include <arch/esp32s31/tiku_runtime_arch.h>
#include <shell/tiku_shell.h>
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#include <string.h>
/** @brief Fixed queue reservations precede the caller-sized TCP service. */
typedef struct {
    tiku_media_store_t *store;
    tiku_mt_tcp_t *tcp;
    uint64_t console_enable,console_stop,last_token;
    int last_result;
    unsigned turn,busy,burst;
    uint64_t burst_start;
    _Alignas(8) uint8_t read[6144],mutation[8192];
    _Alignas(8) uint8_t tcp_storage[];
} service_memory_t;
static tiku_arena_t service_arena;
static service_memory_t *service_memory;
static unsigned service_releasing;
static struct tiku_timer service_timer;
TIKU_PROCESS(media_transfer_process,"Media transfer");
/** @brief Serialized S31 kernel execution excludes trap callbacks and worker threads. */
static int service_owner(void *context)
{ (void)context;return tiku_thread_in_kernel()&&!tiku_s31_in_trap; }
/** @brief Validate the service reservation before dereferencing its PSRAM state. */
static int service_live(void)
{
    tiku_mem_stats_t stats;
    return service_memory&&tiku_arena_stats(&service_arena,&stats)==TIKU_MEM_OK&&
        stats.used_bytes==sizeof(service_memory_t)+tiku_mt_tcp_size();
}
/** @brief Report whether this scheduler retains a shared request or transport. */
static int service_media_idle(service_memory_t *m)
{ return tiku_media_card_read_idle(m->read)&&tiku_media_card_mutation_idle(m->mutation); }
/** @brief Advance the owned request, or dispatch one supported FIFO head. */
static void service_media_step(service_memory_t *m)
{
    if(!tiku_media_card_read_idle(m->read)){(void)tiku_media_card_read_step(m->read,m->store);return;}
    if(!tiku_media_card_mutation_idle(m->mutation)){(void)tiku_media_card_mutation_step(m->mutation,m->store);return;}
    int rc=tiku_media_card_read_step(m->read,m->store);
    if(rc==2)(void)tiku_media_card_mutation_step(m->mutation,m->store);
}
/** @brief Cancel a packet request through its matching serialized media owner. */
static int service_cancel(void *context,tiku_media_request_t *request)
{
    service_memory_t *m=context;
    if(m->store->running==4)return tiku_media_card_mutation_cancel(m->mutation,m->store,request);
    return tiku_media_card_read_cancel(m->read,m->store,request);
}
/** @brief Consume one console-owned command outcome without taking a GUI token. */
static void service_console_result(service_memory_t *m,uint64_t *token)
{
    if(!*token)return;
    int result;if(!tiku_mt_tcp_take(m->tcp,*token,&result)){
        m->last_token=*token;m->last_result=result;*token=0;
    }
}
void tiku_mt_service_step(void)
{
    if(!service_owner(NULL)||service_releasing||!service_live())return;
    service_memory_t *m=service_memory;
    unsigned phase=m->turn++%4;
    if(phase==0||phase==2){
        if(!service_media_idle(m)||m->store->count)service_media_step(m);
    }else if(phase==1){
        uint32_t ms=(uint32_t)(tiku_s31_time()/16000u);
        m->busy=tiku_mt_tcp_step(m->tcp,ms)==TIKU_MT_CONTROL_PENDING;
    }else{
        service_console_result(m,m->console_enable?&m->console_enable:&m->console_stop);
    }
}
/** @brief Schedule at most 32 owner turns or two milliseconds before a timer fallback. */
TIKU_PROCESS_THREAD(media_transfer_process,ev,data)
{
    TIKU_PROCESS_BEGIN();
    for(;;){
        tiku_timer_set_event(&service_timer,1);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev==TIKU_EVENT_POLL||ev==TIKU_EVENT_EXIT||
            (ev==TIKU_EVENT_TIMER&&data==&service_timer&&tiku_timer_expired(&service_timer)));
        if(ev==TIKU_EVENT_EXIT||service_releasing||!service_live())break;
        if(!service_memory->burst)service_memory->burst_start=tiku_s31_time()/16u;
        tiku_mt_service_step();
        if(++service_memory->burst<32&&tiku_s31_time()/16u-service_memory->burst_start<2000&&
           (service_memory->busy||service_memory->store->count||!service_media_idle(service_memory)||
            service_memory->console_enable||service_memory->console_stop))tiku_process_poll(&media_transfer_process);
        else service_memory->burst=0;
    }
    tiku_timer_stop(&service_timer);
    TIKU_PROCESS_END();
}
/** @brief Wake the independently owned transfer process from the console pump. */
static void service_wake(void)
{ if(service_live()&&!service_releasing)tiku_process_poll(&media_transfer_process); }
/** @brief Retry allocator release after callback detachment without accessing arena payloads. */
static int service_release(void)
{
    if(tiku_arena_reset(&service_arena)||tiku_arena_destroy(&service_arena))return TIKU_MFS_BUSY;
    service_memory=NULL;service_releasing=0;return 0;
}
int tiku_mt_service_configure(uint16_t port,const uint8_t key[32],uint64_t *generation)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(service_releasing)return TIKU_MFS_BUSY;
    if(!port||!key||!generation)return TIKU_MFS_ARG;
    unsigned nonzero=0;for(unsigned i=0;i<32;++i)nonzero|=key[i];if(!nonzero)return TIKU_MFS_ARG;
    tiku_media_transfer_status_t legacy;
    if(tiku_media_transfer_server_status(&legacy)||legacy.active)return TIKU_MFS_BUSY;
    tiku_media_store_t *store=tiku_media_card_store();if(!store||store->failed)return TIKU_MFS_NOFS;
    if(service_memory&&!service_live())return TIKU_MFS_STALE;
    if(!service_memory){
        if(tiku_media_card_read_size()>6144||tiku_media_card_mutation_size()>8192||tiku_mt_tcp_align()>8)return TIKU_MFS_LIMIT;
        size_t bytes=sizeof(service_memory_t)+tiku_mt_tcp_size();
        tiku_mem_request_t options={.allocation_class=TIKU_MEM_TRANSIENT};
        if(tiku_tier_arena_create_opts(&service_arena,TIKU_MEM_PSRAM,bytes,93,&options))return TIKU_MFS_SPACE;
        service_memory=tiku_arena_alloc(&service_arena,bytes);
        if(!service_memory){(void)tiku_arena_destroy(&service_arena);return TIKU_MFS_SPACE;}
        service_memory_t *m=service_memory;memset(m,0,offsetof(service_memory_t,read));m->store=store;
        tiku_media_card_read_init(m->read);tiku_media_card_mutation_init(m->mutation);
        tiku_kits_net_tcp_init();
        m->tcp=tiku_mt_tcp_init(m->tcp_storage,tiku_mt_tcp_size(),store,0x4d465403u,service_owner,service_cancel,m);
        if(!m->tcp||tiku_shell_add_pump(service_wake)){
            if(m->tcp)(void)tiku_mt_tcp_unbind(m->tcp);
            service_releasing=1;(void)service_release();return TIKU_MFS_BUSY;
        }
        tiku_process_start(&media_transfer_process,NULL);
        if(!tiku_process_is_running(&media_transfer_process)){
            tiku_shell_remove_pump(service_wake);(void)tiku_mt_tcp_unbind(m->tcp);
            service_releasing=1;(void)service_release();return TIKU_MFS_BUSY;
        }
    }
    if(service_memory->store!=store)return TIKU_MFS_STALE;
    tiku_mt_control_profile_t p={.port=port};memcpy(p.key,key,32);memcpy(p.volume,store->uuid,16);
    int rc=tiku_mt_tcp_configure(service_memory->tcp,&p,generation);memset(&p,0,sizeof p);return rc;
}
int tiku_mt_service_submit(tiku_mt_control_command_t command,uint64_t generation,uint64_t *token)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(service_releasing)return TIKU_MFS_BUSY;
    if(!service_live())return TIKU_MFS_NOFS;
    if(command==TIKU_MT_CONTROL_ENABLE){
        tiku_media_transfer_status_t legacy;
        if(tiku_media_transfer_server_status(&legacy)||legacy.active)return TIKU_MFS_BUSY;
    }
    return tiku_mt_tcp_submit(service_memory->tcp,command,generation,token);
}
int tiku_mt_service_take(uint64_t token,int *result)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(service_releasing)return TIKU_MFS_BUSY;
    if(!service_live())return TIKU_MFS_NOFS;
    int rc=tiku_mt_tcp_take(service_memory->tcp,token,result);
    if(!rc){service_memory->last_token=token;service_memory->last_result=*result;}return rc;
}
int tiku_mt_service_observe(tiku_mt_service_snapshot_t *out)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(!out)return TIKU_MFS_ARG;
    if(service_releasing)return TIKU_MFS_BUSY;
    tiku_mt_service_snapshot_t state={.available=1};
    if(service_memory){
        if(!service_live())return TIKU_MFS_STALE;
        int rc=tiku_mt_tcp_observe(service_memory->tcp,&state.transfer);if(rc)return rc;
        state.last_token=service_memory->last_token;state.last_result=service_memory->last_result;
    }
    *out=state;return 0;
}
int tiku_mt_service_forget(void)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(service_releasing)return service_release();
    if(!service_memory)return 0;
    if(!service_live())return TIKU_MFS_STALE;
    if(!service_media_idle(service_memory))return TIKU_MFS_BUSY;
    int rc=tiku_mt_tcp_forget(service_memory->tcp);if(rc)return rc;
    rc=tiku_mt_tcp_unbind(service_memory->tcp);if(rc)return rc;
    tiku_shell_remove_pump(service_wake);tiku_timer_stop(&service_timer);tiku_process_exit(&media_transfer_process);
    service_releasing=1;return service_release();
}
int tiku_mt_service_start(uint16_t port,const uint8_t key[32])
{
    uint64_t generation;int rc=tiku_mt_service_configure(port,key,&generation);if(rc)return rc;
    if(service_memory->console_enable||service_memory->console_stop)return TIKU_MFS_BUSY;
    rc=tiku_mt_service_submit(TIKU_MT_CONTROL_ENABLE,generation,&service_memory->console_enable);
    return rc?rc:TIKU_MT_CONTROL_PENDING;
}
int tiku_mt_service_stop(void)
{
    if(!service_owner(NULL))return TIKU_MT_CONTROL_OWNER;
    if(service_releasing)return TIKU_MFS_BUSY;
    if(!service_memory)return 0;
    if(!service_live())return TIKU_MFS_STALE;
    tiku_mt_tcp_snapshot_t state;int rc=tiku_mt_tcp_observe(service_memory->tcp,&state);if(rc)return rc;
    if(!state.control.cleanup_required)return 0;
    if(service_memory->console_stop)return TIKU_MT_CONTROL_PENDING;
    rc=tiku_mt_service_submit(TIKU_MT_CONTROL_STOP,state.control.generation,&service_memory->console_stop);
    return rc?rc:TIKU_MT_CONTROL_PENDING;
}
#endif
