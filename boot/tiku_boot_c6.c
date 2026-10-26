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
    tiku_ipc_access_domain_t dma,cpu;
    tiku_ipc_packet_io_t io;
    uint32_t fault_every,attempts,faults;
    uint8_t packets;
#if TIKU_C6_COLD_BOOT_ENABLE
    uint8_t startup;
#endif
} c6 __attribute__((section(".p4.ipc"),aligned(64)));

/** @brief Invalidate logical work when the physical owner's mode or session changes. */
static void lost(void *unused)
{
    (void)unused;
    if(c6.wire.initialized)(void)tiku_ipc_wire_ops.reset(&c6.wire,c6.wire.endpoint);
}
/** @brief Copy a selected corrupt fragment before the owner's synchronous staging copy. */
static tiku_ipc_err_t packet_start(void *context,const void *tx,void *rx,uint16_t length)
{
    const uint8_t *p=tx;
    uint8_t copy[TIKU_IPC_PACKET_SLOT];
    uint8_t input=length==320&&(p[0]|(uint16_t)p[1]<<8)>40&&p[7]==5;
    uint8_t corrupt=input&&c6.fault_every&&(c6.attempts+1u)%c6.fault_every==0;
    tiku_ipc_err_t result;
    if(corrupt){memcpy(copy,p,length);copy[42]^=1;p=copy;}
    result=c6.packet.io.start(context,p,rx,length);
    if(result==TIKU_IPC_OK&&input){c6.attempts++;if(corrupt)c6.faults++;}
    return result;
}
/** @brief Accept copied traffic while the physical peer is in native mode. */
static tiku_ipc_err_t send(void *unused,uint16_t endpoint,uint32_t session,
    const void *data,uint16_t length)
{
    (void)unused;
    if(!c6.packets||c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_NATIVE)return TIKU_IPC_AGAIN;
    return tiku_ipc_wire_ops.send(&c6.wire,endpoint,session,data,length);
}
/** @brief Retain private backing through the framed service while native is ready. */
static tiku_ipc_err_t start(void *unused,uint16_t endpoint,const tiku_ipc_transfer_t *transfer)
{
    (void)unused;
    if(!c6.packets||c6.owner.state!=TIKU_SDIO_READY||c6.owner.mode!=TIKU_SDIO_NATIVE)return TIKU_IPC_AGAIN;
    return tiku_ipc_wire_ops.start(&c6.wire,endpoint,transfer);
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
    if(c6.owner.state==TIKU_SDIO_FAILED&&c6.owner.wanted==TIKU_SDIO_NATIVE)
        (void)tiku_ipc_sdio_mode(&c6.owner,TIKU_SDIO_LOADER);
#endif
    tiku_ipc_sdio_step(&c6.owner);
    if(c6.packets&&c6.owner.state==TIKU_SDIO_READY&&c6.owner.mode==TIKU_SDIO_NATIVE)
        tiku_ipc_wire_pump(&c6.wire,budget);
    (void)tiku_ipc_service_at(endpoint,1);
}
/** @brief Remove a private transfer association; DMA uses separately owned stages. */
static uint8_t quiesce(void *unused,uint16_t endpoint,const tiku_ipc_transfer_t *transfer)
{(void)unused;return tiku_ipc_wire_ops.quiesce(&c6.wire,endpoint,transfer);}
/** @brief Publish cached wire counters without accessing SDIO registers. */
static void status(void *unused,tiku_ipc_status_t *out)
{(void)unused;tiku_ipc_wire_ops.status(&c6.wire,out);}
/** @brief Invalidate work and recover the physical controller to the resident loader. */
static tiku_ipc_err_t reset(void *unused,uint16_t endpoint)
{
    (void)unused;
    (void)tiku_ipc_wire_ops.reset(&c6.wire,endpoint);
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
    if(!link||tiku_ipc_wire_init(&c6.wire,link,"c6/bulk",endpoint,epoch,256,NULL)!=TIKU_IPC_OK)return -1;
    return 0;
}
tiku_ipc_sdio_t *tiku_boot_c6_owner(void){return &c6.owner;}
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
