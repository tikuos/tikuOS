/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_arch.c - native C6 target FIFOs, fixed descriptors and final access.
 *
 * Registers follow ESP-IDF 4d59230 esp32c6 SDIO SLC, HOST, HINF and PCR.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include "tiku_sdio_arch.h"
#include <hal/tiku_sdio_diag.h>
#include <string.h>
#define SLC 0x60017000u
#define HOST 0x60018000u
#define OWN (1u<<31)
#define EOF_BIT (1u<<30)
#define RX_DONE (1u<<14)
#define TX_DONE (1u<<17)
#define PARK (1u<<31)
#define STOP (1u<<28)
#define START (1u<<29)

static struct{volatile uint32_t control;uint32_t address,next,padding[13];}
    rx_desc __attribute__((aligned(64))),tx_desc __attribute__((aligned(64)));
static struct {
    uint32_t sent;
    uint16_t capacity;
    uint8_t active,rx_done,tx_done;
} engine;
static uint64_t committed_epoch;
static tiku_ipc_access_domain_t domain;
static tiku_ipc_packet_io_t io;
/** @brief Publish descriptors and payload before enabling the target DMA. */
static void barrier(void)
{
#if defined(TIKU_C6_SDIO_HOST)
    __asm__ volatile("":::"memory");
#else
    __asm__ volatile("fence iorw,iorw":::"memory");
#endif
}
/** @brief Arm separate fixed transmit/receive stages for one full packet exchange. */
static tiku_ipc_err_t start(void *unused,const void *tx,void *rx,uint16_t capacity)
{
    (void)unused;
    if(engine.active)return TIKU_IPC_AGAIN;
    if(capacity!=TIKU_IPC_PACKET_SLOT||
        tiku_ipc_domain_check(&domain,1,1,tx,capacity,TIKU_IPC_INPUT)!=TIKU_IPC_OK||
        tiku_ipc_domain_check(&domain,1,1,rx,capacity,TIKU_IPC_OUTPUT)!=TIKU_IPC_OK)
        return TIKU_IPC_INVALID;
    /* The send FIFO retains packet EOF state across descriptor lists. */
    C6_REG(SLC)|=2u;barrier();C6_REG(SLC)&=~2u;
    memset(&rx_desc,0,sizeof rx_desc);memset(&tx_desc,0,sizeof tx_desc);
    rx_desc.address=(uint32_t)(uintptr_t)rx;rx_desc.control=OWN|capacity;
    tx_desc.address=(uint32_t)(uintptr_t)tx;
    tx_desc.control=OWN|EOF_BIT|capacity|((uint32_t)capacity<<14);
    engine.capacity=capacity;engine.rx_done=engine.tx_done=0;engine.active=1;
    C6_REG(SLC+0x10)=RX_DONE|TX_DONE;
    barrier();
    C6_REG(SLC+0x48)=(uint32_t)(uintptr_t)&rx_desc;
    C6_REG(SLC+0x44)=START;
    C6_REG(SLC+0x64)=(1u<<14)|1u;
    C6_REG(SLC+0x40)=(uint32_t)(uintptr_t)&tx_desc;
    C6_REG(SLC+0x3c)=START;
    engine.sent=(engine.sent+capacity)&0xfffffu;
    C6_REG(SLC+0xf4)=(1u<<20)|engine.sent;
    return TIKU_IPC_OK;
}
/** @brief Attest both DMA descriptors have returned before packet copy-out. */
static tiku_ipc_err_t poll(void *unused)
{
    uint32_t events;
    (void)unused;
    if(!engine.active)return TIKU_IPC_INVALID;
    events=C6_REG(SLC+4);barrier();
    if((events&RX_DONE)&&!(rx_desc.control&OWN))engine.rx_done=1;
    if((events&TX_DONE)&&!(tx_desc.control&OWN))engine.tx_done=1;
    if(!engine.rx_done||!engine.tx_done)return TIKU_IPC_AGAIN;
    if(((rx_desc.control>>14)&0x3fffu)!=engine.capacity)return TIKU_IPC_IO;
    C6_REG(SLC+0x10)=RX_DONE|TX_DONE;
    barrier();engine.active=0;return TIKU_IPC_OK;
}
/** @brief Confirm both target engines are parked before allowing stage reuse. */
static uint8_t abort_transfer(void *unused)
{
    (void)unused;
    if(!engine.active)return 1;
    C6_REG(SLC+0x3c)=STOP;C6_REG(SLC+0x44)=STOP;
    for(unsigned n=0;n<4096;n++){
        if((C6_REG(SLC+0x3c)&PARK)&&(C6_REG(SLC+0x44)&PARK)){
            barrier();engine.active=0;return 1;
        }
    }
    return 0;
}
/** @brief Keep one offered packet armed until a host transfer half completes. */
static uint8_t listening(void *unused)
{
    (void)unused;
    return engine.active&&!engine.rx_done&&!engine.tx_done&&
        !(C6_REG(SLC+4)&(RX_DONE|TX_DONE));
}
int tiku_c6_sdio_init(void)
{
    for(unsigned p=18;p<=23;p++){
        uint32_t value=C6_REG(0x60090004u+4u*p);
        value&=~((7u<<12)|(1u<<7)|(1u<<8));
        C6_REG(0x60090004u+4u*p)=value|(1u<<9)|(3u<<10)|(p==19?0:1u<<8);
    }
    C6_REG(0x600960b0u)|=3u;C6_REG(0x600960b0u)&=~2u;
    C6_REG(HOST+0x1f0)=(C6_REG(HOST+0x1f0)&~0xfffffu)|0x000f801fu;
    C6_REG(0x60016004u)=(C6_REG(0x60016004u)&~(0xfffu<<12))|(0x232u<<12)|2u;
    C6_REG(SLC+0x0c)=0;C6_REG(SLC+0x10)=UINT32_MAX;
    C6_REG(SLC)=((C6_REG(SLC)&~((1u<<14)|(3u<<4)))|(1u<<6)|3u);
    C6_REG(SLC)&=~3u;
    C6_REG(SLC+0x70)&=~0x70u;C6_REG(SLC+0xa8)|=1u;
    C6_REG(SLC+0x64)=1u<<12;C6_REG(SLC+0xf4)=1u<<20;
    engine.sent=0;engine.active=0;
    domain=(tiku_ipc_access_domain_t){(uintptr_t)__native_image_start,65536,1,1,64,0,3};
    io=(tiku_ipc_packet_io_t){start,poll,abort_transfer,NULL,&domain,1,1,128,listening};
    committed_epoch=0;
    return 0;
}
const tiku_ipc_packet_io_t *tiku_c6_sdio_io(void){return &io;}
uint64_t tiku_c6_sdio_epoch(void)
{return committed_epoch;}
int tiku_c6_sdio_commit_epoch(uint64_t epoch)
{
    if(!epoch)return -1;
    committed_epoch=epoch;barrier();return 0;
}
uint32_t tiku_c6_sdio_observe(unsigned index)
{
    switch(index){
    case 0:return engine.active|(uint32_t)engine.rx_done<<1|(uint32_t)engine.tx_done<<2|
        (C6_REG(SLC+0x24)&0xffffu)<<8|
        ((C6_REG(SLC+0x44)&PARK)?1u<<24:0)|
        ((C6_REG(SLC+0x3c)&PARK)?1u<<25:0);
    case 1:return C6_REG(SLC+4);
    case 2:return rx_desc.control;
    case 3:return tx_desc.control;
    case 4:return C6_REG(SLC+0x44);
    case 5:return C6_REG(SLC+0x3c);
    default:return 0;
    }
}
