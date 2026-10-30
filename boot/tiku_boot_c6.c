/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_c6.c - boot assembly of the P4 hardware SDIO mailbox backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_boot_c6.h"
#include <arch/esp32p4/tiku_sdio_owner_arch.h>
#include <arch/esp32p4/tiku_sdmmc_arch.h>
#include <services/ipc/tiku_ipc_sdio_packet.h>
#include <services/ipc/tiku_ipc_wire.h>
#include <services/ipc/tiku_ipc_bus.h>
#if TIKU_DRV_WIFI_IPC_ENABLE
#include <services/ipc/tiku_ipc_mux.h>
#include <services/ipc/tiku_ipc_radio.h>
#endif
#include <hal/tiku_ipc_epoch_hal.h>
#include <hal/tiku_c6_loader_hal.h>
#include <kernel/memory/tiku_mem.h>
#include <string.h>
extern uint8_t __ipc_device_start[],__ipc_device_end[];
static struct {
    tiku_ipc_sdio_t owner;
    tiku_p4_sdio_owner_t driver;
    tiku_ipc_sdio_packet_t packet;
    tiku_ipc_bus_t bus;
    tiku_ipc_wire_t wire;
#if TIKU_DRV_WIFI_IPC_ENABLE
    tiku_ipc_wire_t control,data;
    tiku_ipc_mux_t mux;
    uint8_t cursor,physical_ready,service_seen,retries;
    uint32_t physical_session;
    tiku_clock_time_t retry_at;
#endif
    tiku_ipc_access_domain_t dma,cpu;
    tiku_ipc_packet_io_t io;
    uint32_t fault_every,attempts,faults;
    uint8_t packets;
#if TIKU_C6_COLD_BOOT_ENABLE
    uint8_t startup;
#endif
} c6 __attribute__((section(".p4.ipc"),aligned(64)));

/** @brief Select the wire associated with a boot endpoint index. */
static tiku_ipc_wire_t *endpoint_wire(uint16_t endpoint)
{
#if TIKU_DRV_WIFI_IPC_ENABLE
    if(endpoint==c6.control.endpoint)return &c6.control;
    if(endpoint==c6.data.endpoint)return &c6.data;
#else
    (void)endpoint;
#endif
    return &c6.wire;
}
/** @brief Invalidate logical work when the physical owner's mode or session changes. */
static void lost(void *unused)
{
    (void)unused;
#if TIKU_DRV_WIFI_IPC_ENABLE
    if(c6.control.initialized)(void)tiku_ipc_wire_ops.reset(&c6.control,c6.control.endpoint);
    if(c6.data.initialized)(void)tiku_ipc_wire_ops.reset(&c6.data,c6.data.endpoint);
#endif
    if(c6.wire.initialized)(void)tiku_ipc_wire_ops.reset(&c6.wire,c6.wire.endpoint);
}
/** @brief Copy a selected corrupt fragment before the owner's synchronous staging copy. */
static tiku_ipc_err_t packet_start(void *context,const void *tx,void *rx,uint16_t length)
{
    const uint8_t *p=tx;
    uint8_t copy[TIKU_IPC_PACKET_SLOT];
#if TIKU_DRV_WIFI_IPC_ENABLE
    const unsigned prefix=1;
#else
    const unsigned prefix=0;
#endif
    uint8_t input=length==320&&(p[0]|(unsigned)p[1]<<8)>40+prefix&&p[7+prefix]==5;
    uint8_t corrupt=input&&c6.fault_every&&(c6.attempts+1u)%c6.fault_every==0;
    tiku_ipc_err_t result;
    if(corrupt){memcpy(copy,p,length);copy[42+prefix]^=1;p=copy;}
    result=c6.packet.io.start(context,p,rx,length);
#if TIKU_DRV_WIFI_IPC_ENABLE
    tiku_radio_wipe(copy,sizeof copy);
#endif
    if(result==TIKU_IPC_OK&&input){c6.attempts++;if(corrupt)c6.faults++;}
    return result;
}
/** @brief Accept copied traffic while the physical peer is in native mode. */
static tiku_ipc_err_t send(void *unused,uint16_t endpoint,uint32_t session,
    const void *data,uint16_t length)
{
    (void)unused;
    if(!c6.packets||c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_NATIVE)return TIKU_IPC_AGAIN;
    return tiku_ipc_wire_ops.send(endpoint_wire(endpoint),endpoint,session,data,length);
}
/** @brief Retain private backing through the framed service while native is ready. */
static tiku_ipc_err_t start(void *unused,uint16_t endpoint,const tiku_ipc_transfer_t *transfer)
{
    (void)unused;
    if(!c6.packets||c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_NATIVE)return TIKU_IPC_AGAIN;
    return tiku_ipc_wire_ops.start(endpoint_wire(endpoint),endpoint,transfer);
}
/** @brief Drive bounded discovery/recovery even when no application has an endpoint. */
static void progress(void *unused,uint16_t endpoint,uint16_t budget)
{
    (void)unused;
#if TIKU_C6_COLD_BOOT_ENABLE
    if(!c6.startup){
        c6.startup=1;
        (void)tiku_ipc_sdio_mode(&c6.owner,TIKU_SDIO_NATIVE);
    }
    if(c6.owner.state==TIKU_SDIO_FAILED&&c6.owner.wanted==TIKU_SDIO_NATIVE) {
#if TIKU_DRV_WIFI_IPC_ENABLE
        if(!c6.service_seen||c6.retries==3)
#endif
            (void)tiku_ipc_sdio_mode(&c6.owner,TIKU_SDIO_LOADER);
    }
#endif
    tiku_ipc_sdio_step(&c6.owner);
#if TIKU_DRV_WIFI_IPC_ENABLE
    uint8_t ready=c6.owner.state==TIKU_SDIO_READY&&c6.owner.mode==TIKU_SDIO_NATIVE;
    if(c6.physical_session!=c6.owner.session||(c6.physical_ready&&!ready)) {
        lost(NULL);
        if(c6.physical_ready&&!ready)c6.retry_at=tiku_clock_time()+TIKU_CLOCK_SECOND;
    }
    c6.physical_session=c6.owner.session;
    c6.physical_ready=ready;
    if(ready&&c6.control.ready){c6.service_seen=1;c6.retries=0;}
    if(c6.packets&&c6.service_seen&&c6.retries<3&&
       c6.owner.requested==TIKU_SDIO_NATIVE&&
       (c6.owner.state==TIKU_SDIO_FAILED||
        (c6.owner.state==TIKU_SDIO_READY&&c6.owner.mode==TIKU_SDIO_LOADER))&&
       !TIKU_CLOCK_LT(tiku_clock_time(),c6.retry_at)) {
        c6.retries++;
        c6.retry_at=tiku_clock_time()+5*TIKU_CLOCK_SECOND;
        (void)tiku_ipc_sdio_mode(&c6.owner,TIKU_SDIO_NATIVE);
    }
#endif
    if(c6.packets&&c6.owner.state==TIKU_SDIO_READY&&c6.owner.mode==TIKU_SDIO_NATIVE)
    {
#if TIKU_DRV_WIFI_IPC_ENABLE
        tiku_ipc_wire_pump(&c6.control,budget);
        if(c6.cursor++&1){tiku_ipc_wire_pump(&c6.data,budget);tiku_ipc_wire_pump(&c6.wire,budget);}
        else {tiku_ipc_wire_pump(&c6.wire,budget);tiku_ipc_wire_pump(&c6.data,budget);}
#else
        tiku_ipc_wire_pump(&c6.wire,budget);
#endif
    }
    (void)tiku_ipc_service_at(endpoint,1);
}
/** @brief Remove a private transfer association; DMA uses separately owned stages. */
static uint8_t quiesce(void *unused,uint16_t endpoint,const tiku_ipc_transfer_t *transfer)
{(void)unused;return tiku_ipc_wire_ops.quiesce(endpoint_wire(endpoint),endpoint,transfer);}
/** @brief Publish cached wire counters without accessing SDIO registers. */
static void status(void *context,tiku_ipc_status_t *out)
{
    tiku_ipc_wire_t *wire=&c6.wire;
#if TIKU_DRV_WIFI_IPC_ENABLE
    const uint8_t *channel=context;
    if(channel&&*channel==1)wire=&c6.control;
    if(channel&&*channel==2)wire=&c6.data;
#else
    (void)context;
#endif
    tiku_ipc_wire_ops.status(wire,out);
}
/** @brief Invalidate work and recover the physical controller to the resident loader. */
static tiku_ipc_err_t reset(void *unused,uint16_t endpoint)
{
    (void)unused;
    (void)tiku_ipc_wire_ops.reset(endpoint_wire(endpoint),endpoint);
    return tiku_ipc_sdio_mode(&c6.owner,TIKU_SDIO_LOADER);
}
const tiku_ipc_backend_ops_t tiku_boot_c6_ops={send,start,progress,quiesce,64,512,
    status,reset,TIKU_IPC_RESET_CONNECTION};
int tiku_boot_c6_init(uint16_t endpoint)
{
    uint64_t epoch;uint32_t session;tiku_link_t *link;
    /* Four bounded register steps fit one burst; recovery has distinct budgets. */
    const tiku_sdio_profile_t profile={64,128,8,512,256};
    c6.packets=1;
#if TIKU_C6_COLD_BOOT_ENABLE
    c6.startup=0;
#endif
    if(tiku_ipc_epoch_fresh(&epoch)!=TIKU_IPC_OK)return -1;
    session=(uint32_t)epoch;if(!session||session==UINT32_MAX)session=1;
    c6.dma=(tiku_ipc_access_domain_t){(uintptr_t)__ipc_device_start,
        (uintptr_t)(__ipc_device_end-__ipc_device_start),1,1,64,64,3};
    c6.cpu=c6.dma;c6.cpu.engine=0;
    if(tiku_ipc_sdio_open(&c6.owner,&tiku_p4_sdio_owner_ops,&c6.driver,&c6.dma,1,1,
        session,TIKU_SDIO_NATIVE,&profile)!=TIKU_IPC_OK||
        tiku_ipc_sdio_packet_init(&c6.packet,&c6.owner,&c6.cpu,1,lost,NULL))return -1;
    c6.io=c6.packet.io;c6.io.start=packet_start;
    link=tiku_ipc_bus_open(&c6.bus,&c6.io);
#if TIKU_DRV_WIFI_IPC_ENABLE
    if(!link||tiku_ipc_mux_init(&c6.mux,link))return -1;
    tiku_link_t *bulk_link=tiku_ipc_mux_open(&c6.mux,0,NULL,NULL);
    tiku_link_t *control_link=tiku_ipc_mux_open(&c6.mux,1,NULL,NULL);
    tiku_link_t *data_link=tiku_ipc_mux_open(&c6.mux,2,NULL,NULL);
    if(tiku_ipc_wire_init(&c6.wire,bulk_link,"c6/bulk",endpoint,epoch,255,NULL)||
       tiku_ipc_wire_init(&c6.control,control_link,"c6/radio",endpoint+1,epoch,255,&tiku_radio_control_sink)||
       tiku_ipc_wire_init(&c6.data,data_link,"c6/wifi",endpoint+2,epoch,255,&tiku_radio_data_sink))return -1;
    tiku_radio_service_start(&c6.control,&c6.data);
#else
    if(!link||tiku_ipc_wire_init(&c6.wire,link,"c6/bulk",endpoint,epoch,256,NULL)!=TIKU_IPC_OK)return -1;
#endif
    return 0;
}
tiku_ipc_sdio_t *tiku_boot_c6_owner(void){return &c6.owner;}
uint16_t tiku_boot_c6_factory_length(void){return c6.driver.available;}
void tiku_boot_c6_factory_cut(void){c6.driver.cut=1;}
uint32_t tiku_boot_c6_factory_cuts(void){return c6.driver.factory_cuts;}
tiku_ipc_err_t tiku_boot_c6_adopt_loader(void)
{
    if(!c6.owner.claimed||c6.owner.mode!=TIKU_SDIO_FACTORY)return TIKU_IPC_INVALID;
    c6.driver.adopt=1;return TIKU_IPC_OK;
}
void tiku_boot_c6_factory_wait_restart(void)
{
    if(c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_FACTORY)return;
    c6.driver.booting=1;c6.driver.reset_until=tiku_clock_time()+10u*TIKU_CLOCK_SECOND;
    c6.owner.lost_at=c6.driver.reset_until+c6.owner.profile.detect;
}
uint16_t tiku_boot_c6_factory_info(void *out,uint16_t capacity)
{
    uint16_t size=tiku_boot_c6_factory_length();
    if(!out||size>capacity||c6.owner.state!=TIKU_SDIO_READY||
       c6.owner.mode!=TIKU_SDIO_FACTORY)return 0;
    memcpy(out,c6.driver.scratch,size);return size;
}
uint16_t tiku_boot_c6_loader_info(void *out,uint16_t capacity)
{
    uint32_t bytes=tiku_c6_get32(c6.driver.scratch+24);
    uint16_t length;
    if(bytes>480)return 0;
    length=(uint16_t)(32u+bytes);
    if(c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_LOADER||
        length>capacity||!out||!tiku_c6_response(c6.driver.scratch,length,TIKU_C6_INFO,0))return 0;
    memcpy(out,c6.driver.scratch,length);return length;
}
void tiku_boot_c6_fault_every(uint32_t interval)
{c6.fault_every=interval;c6.attempts=c6.faults=0;}
uint32_t tiku_boot_c6_faults(void){return c6.faults;}
void tiku_boot_c6_recovery(uint32_t out[4])
{if(out)memcpy(out,c6.driver.recovery,sizeof c6.driver.recovery);}
void tiku_boot_c6_trace(uint32_t out[5])
{if(out)memcpy(out,c6.driver.trace,sizeof c6.driver.trace);}
tiku_ipc_err_t tiku_boot_c6_packet_gate(uint8_t enabled)
{
    if(!enabled&&(c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_LOADER))return TIKU_IPC_BUSY;
    c6.packets=!!enabled;return TIKU_IPC_OK;
}
