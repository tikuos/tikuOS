/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdmmc_arch.c - P4 slot1 commands, clocking and one-burst IDMAC ownership.
 *
 * Registers follow ESP-IDF 4d59230 hw_ver1 sdmmc and clock definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sdmmc_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_systimer_arch.h"
#include <hal/tiku_cpu.h>
#include <string.h>

#define BASE 0x50083000u
#define REG(n) (BASE+(n))
#define CTRL REG(0)
#define CMD REG(0x2c)
#define RINT REG(0x44)
#define IDSTS REG(0x8c)
#define BMOD REG(0x80)
#define START (1u<<31)
#define SLOT (1u<<16)
#define UPDATE (1u<<21)
#define WAIT_DATA (1u<<13)
#define CMD_ERRORS 0xbfc2u
#define DMA_ERRORS ((1u<<2)|(1u<<4)|(1u<<5))
#define OWN (1u<<31)
#define SYS_CLOCK 0x500e6018u
#define CORE_CLOCK 0x500e6034u
#define PHASE_CLOCK 0x500e6038u
#define RESET 0x5011104cu
#define SYS_BIT (1u<<14)
#define RESET_BIT (1u<<28)
#define CORE_BITS (7u<<22)
#define PHASE_BITS 0x3fffff00u
#define PIN_MUX(n) (0x500e1004u+4u*(n))
#define PIN_CFG(n) (0x500e0074u+4u*(n))
#define PIN_OUT(n) (0x500e0558u+4u*(n))
#define PIN_IN(n) (0x500e0158u+4u*(n))
#define PIN_LP(n) (0x5012b008u+4u*(n))
#define BANK(n,a,b) ((n)<32u?(a):(b))
#define BIT(n) (1u<<((n)%32u))
#define SPINS 200000u
#define COMMAND_US 3000u

static const unsigned pins[6]={14,15,16,17,18,19};
static const unsigned signals[6]={2,3,4,5,0,1};
static struct {uint32_t mux,cfg,out,lp,enable,level;} saved[6];
static uint8_t peer_power_owned;
static uint32_t saved_in[8],saved_clock[4];
static struct {
    uint32_t control,size,address,next,padding[12];
} descriptor __attribute__((section(".p4.ipc.sdmmc_descriptor"),aligned(64)));
static tiku_p4_sdmmc_status_t state;
static void *stage;
static uint16_t stage_capacity,stage_length;
static uint8_t stage_write;
static uint32_t seen_raw,seen_dma;
static uint8_t tail_stage[64] __attribute__((section(".p4.ipc"),aligned(64)));
static struct {
    uint8_t *buffer;
    uint32_t address;
    uint16_t prefix;
    uint8_t tail,function,increment,write,copy_out;
} follow;
#ifndef TIKU_P4_SDMMC_COPY
#define TIKU_P4_SDMMC_COPY memcpy
#endif

/** @brief Change one register field while preserving unrelated users. */
static void bits(uint32_t address,uint32_t mask,uint32_t value)
{ TIKU_P4_REG_WRITE(address,(TIKU_P4_REG_READ(address)&~mask)|(value&mask)); }
/** @brief Poll a reset/command field with both iteration and timer bounds. */
static uint8_t clear_wait(uint32_t address,uint32_t mask)
{
    uint64_t began,now;
    if(tiku_p4_systimer_read(&began))return 0;
    for(unsigned n=0;n<SPINS;n++){
        if(!(TIKU_P4_REG_READ(address)&mask))return 1;
        if(tiku_p4_systimer_read(&now)||((now-began)&TIKU_P4_SYSTIMER_MASK)>COMMAND_US*16u)break;
    }
    return 0;
}
/** @brief Transfer clock divider fields into the card interface. */
static tiku_ipc_err_t update_clock(void)
{
    TIKU_P4_REG_WRITE(RINT,UINT32_MAX);
    TIKU_P4_REG_WRITE(CMD,START|SLOT|UPDATE|WAIT_DATA);
    return clear_wait(CMD,START)&&!(TIKU_P4_REG_READ(RINT)&(1u<<12))?
        TIKU_IPC_OK:TIKU_IPC_TIMEOUT;
}
/** @brief Lease six slot1 pads and keep the board-reserved peer enable driven. */
static void pads_open(void)
{
    for(unsigned i=0;i<6;i++){
        unsigned p=pins[i];
        saved[i].mux=TIKU_P4_REG_READ(PIN_MUX(p));
        saved[i].cfg=TIKU_P4_REG_READ(PIN_CFG(p));
        saved[i].out=TIKU_P4_REG_READ(PIN_OUT(p));
        saved[i].lp=p<16?TIKU_P4_REG_READ(PIN_LP(p)):0;
        saved[i].enable=TIKU_P4_REG_READ(BANK(p,0x500e0020u,0x500e002cu))&BIT(p);
        saved[i].level=TIKU_P4_REG_READ(BANK(p,0x500e0004u,0x500e0010u))&BIT(p);
        TIKU_P4_REG_WRITE(BANK(p,0x500e0028u,0x500e0034u),BIT(p));
        if(p<16)bits(PIN_LP(p),1u<<4,0);
        bits(PIN_MUX(p),(7u<<12)|(1u<<7)|(1u<<8)|(1u<<9),
            (1u<<12)|(1u<<9)|(p==18?0:1u<<8));
        bits(PIN_CFG(p),(1u<<2)|(7u<<7)|(31u<<13),0);
        TIKU_P4_REG_WRITE(PIN_OUT(p),signals[i]);
    }
    for(unsigned i=0;i<6;i++){
        saved_in[i]=TIKU_P4_REG_READ(PIN_IN(i));
        if(i)TIKU_P4_REG_WRITE(PIN_IN(i),(1u<<7)|(i==1?19u:12u+i));
    }
    saved_in[6]=TIKU_P4_REG_READ(PIN_IN(127));
    saved_in[7]=TIKU_P4_REG_READ(PIN_IN(131));
    TIKU_P4_REG_WRITE(PIN_IN(127),(1u<<7)|0x3cu);
    TIKU_P4_REG_WRITE(PIN_IN(131),(1u<<7)|0x3cu);
    if(!peer_power_owned){
        TIKU_P4_REG_WRITE(0x500e0014u,BIT(54));
        bits(PIN_MUX(54),(7u<<12)|(1u<<7)|(1u<<8)|(1u<<9),(1u<<12)|(1u<<9));
        bits(PIN_CFG(54),(1u<<2)|(7u<<7)|(31u<<13),0);
        TIKU_P4_REG_WRITE(PIN_OUT(54),256u);
        tiku_p4_sdmmc_peer_enable(1);
        peer_power_owned=1;
    }
}
/** @brief Restore pad routing and output state after the local engine is stopped. */
static void pads_close(void)
{
    for(unsigned i=0;i<6;i++)TIKU_P4_REG_WRITE(PIN_IN(i),saved_in[i]);
    TIKU_P4_REG_WRITE(PIN_IN(127),saved_in[6]);
    TIKU_P4_REG_WRITE(PIN_IN(131),saved_in[7]);
    for(unsigned i=0;i<6;i++){
        unsigned p=pins[i];
        TIKU_P4_REG_WRITE(BANK(p,0x500e0028u,0x500e0034u),BIT(p));
        TIKU_P4_REG_WRITE(BANK(p,saved[i].level?0x500e0008u:0x500e000cu,
                                  saved[i].level?0x500e0014u:0x500e0018u),BIT(p));
        TIKU_P4_REG_WRITE(PIN_CFG(p),saved[i].cfg);
        TIKU_P4_REG_WRITE(PIN_OUT(p),saved[i].out);
        TIKU_P4_REG_WRITE(PIN_MUX(p),saved[i].mux);
        if(p<16)TIKU_P4_REG_WRITE(PIN_LP(p),saved[i].lp);
        if(saved[i].enable)TIKU_P4_REG_WRITE(BANK(p,0x500e0024u,0x500e0030u),BIT(p));
    }
}

tiku_ipc_err_t tiku_p4_sdmmc_boot_quiesce(void)
{
    if(state.opened)return TIKU_IPC_BUSY;
    if(!(TIKU_P4_REG_READ(SYS_CLOCK)&SYS_BIT))return TIKU_IPC_OK;
    bits(BMOD,1u<<7,0);
    bits(CTRL,7u|(1u<<25)|(1u<<5),7u);bits(BMOD,1u,1u);
    if(!clear_wait(CTRL,7u)||!clear_wait(BMOD,1u)||
       (TIKU_P4_REG_READ(IDSTS)&(15u<<13)))return TIKU_IPC_BUSY;
    bits(RESET,RESET_BIT,RESET_BIT);bits(RESET,RESET_BIT,0);
    bits(CORE_CLOCK,CORE_BITS,0);bits(SYS_CLOCK,SYS_BIT,0);
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_p4_sdmmc_open(void)
{
    if(state.opened)return TIKU_IPC_BUSY;
    saved_clock[0]=TIKU_P4_REG_READ(SYS_CLOCK);
    saved_clock[1]=TIKU_P4_REG_READ(CORE_CLOCK);
    saved_clock[2]=TIKU_P4_REG_READ(PHASE_CLOCK);
    saved_clock[3]=TIKU_P4_REG_READ(RESET);
    /* An enabled controller may belong to a different slot owner. */
    if(saved_clock[0]&SYS_BIT)return TIKU_IPC_BUSY;
    state=(tiku_p4_sdmmc_status_t){.opened=1,.width=1};
    pads_open();
    bits(SYS_CLOCK,SYS_BIT,SYS_BIT);
    bits(CORE_CLOCK,CORE_BITS,1u<<24);
    /* 160 MHz / 4 = 40 MHz CIU; drive phase 180 degrees, sample phase zero. */
    bits(PHASE_CLOCK,PHASE_BITS,(3u<<9)|(1u<<13)|(3u<<17)|(1u<<23)|(7u<<27));
    bits(PHASE_CLOCK,1u<<8,1u<<8);bits(PHASE_CLOCK,1u<<8,0);
    bits(RESET,RESET_BIT,RESET_BIT);bits(RESET,RESET_BIT,0);
    TIKU_P4_REG_WRITE(CTRL,7);
    TIKU_P4_REG_WRITE(BMOD,1);
    if(!clear_wait(CTRL,7)||!clear_wait(BMOD,1)){
        state.errors++;return TIKU_IPC_TIMEOUT;
    }
    TIKU_P4_REG_WRITE(REG(0x24),0);
    TIKU_P4_REG_WRITE(REG(0x90),0x337u);
    TIKU_P4_REG_WRITE(REG(0x14),UINT32_MAX);
    TIKU_P4_REG_WRITE(REG(0x4c),(3u<<28)|(63u<<16)|64u);
    TIKU_P4_REG_WRITE(REG(0x18),0);
    return tiku_p4_sdmmc_clock(400000);
}

tiku_ipc_err_t tiku_p4_sdmmc_clock(uint32_t hz)
{
    unsigned divider;
    if(!state.opened||state.active||hz<400000||hz>20000000)return TIKU_IPC_INVALID;
    divider=(40000000u+2u*hz-1u)/(2u*hz);
    TIKU_P4_REG_WRITE(REG(0x10),0);
    if(update_clock()!=TIKU_IPC_OK)return TIKU_IPC_TIMEOUT;
    TIKU_P4_REG_WRITE(REG(0x0c),4u);
    TIKU_P4_REG_WRITE(REG(0x08),divider<<8);
    if(update_clock()!=TIKU_IPC_OK)return TIKU_IPC_TIMEOUT;
    TIKU_P4_REG_WRITE(REG(0x10),2u);
    if(update_clock()!=TIKU_IPC_OK)return TIKU_IPC_TIMEOUT;
    state.hz=40000000u/(2u*divider);
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_p4_sdmmc_command(uint8_t index,uint32_t arg,uint32_t *reply)
{
    uint64_t began,now;
    uint32_t status=0,flags=START|SLOT|WAIT_DATA;
    if(!state.opened||state.active||index>63||!reply)return TIKU_IPC_INVALID;
    if(index)flags|=(1u<<6)|(index==5?0:1u<<8);
    else flags|=1u<<15;
    if(!clear_wait(CMD,START)||tiku_p4_systimer_read(&began))return TIKU_IPC_TIMEOUT;
    TIKU_P4_REG_WRITE(RINT,UINT32_MAX);
    TIKU_P4_REG_WRITE(REG(0x28),arg);
    TIKU_P4_REG_WRITE(CMD,flags|index);
    state.commands++;
    for(unsigned n=0;n<SPINS;n++){
        status=TIKU_P4_REG_READ(RINT);
        if(status&((1u<<2)|CMD_ERRORS))break;
        if(tiku_p4_systimer_read(&now)||((now-began)&TIKU_P4_SYSTIMER_MASK)>COMMAND_US*16u)break;
    }
    state.raw=status;*reply=TIKU_P4_REG_READ(REG(0x30));
    state.response=*reply;state.argument=arg;
    if(!(status&(1u<<2))||(status&CMD_ERRORS)){
        state.errors++;return TIKU_IPC_IO;
    }
    return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_p4_sdmmc_byte(uint8_t write,uint8_t function,uint32_t address,uint8_t *value)
{
    uint32_t reply;
    tiku_ipc_err_t result;
    if(!value||write>1||function>7||address>0x1ffffu)return TIKU_IPC_INVALID;
    result=tiku_p4_sdmmc_command(52,(write?1u<<31:0)|((uint32_t)function<<28)|
                               (address<<9)|(write?*value:0),&reply);
    if(result!=TIKU_IPC_OK)return result;
    if(reply&0xcb00u)return TIKU_IPC_IO;
    *value=(uint8_t)reply;return TIKU_IPC_OK;
}

tiku_ipc_err_t tiku_p4_sdmmc_width(uint8_t width)
{
    if(!state.opened||state.active||(width!=1&&width!=4))return TIKU_IPC_INVALID;
    TIKU_P4_REG_WRITE(REG(0x18),width==4?2u:0);
    state.width=width;return TIKU_IPC_OK;
}

/** @brief Arm one word-sized prefix or a final one-to-three-byte transaction. */
static tiku_ipc_err_t start_chunk(uint8_t write,uint8_t function,uint32_t address,
    uint8_t increment,void *buffer,uint16_t length,uint16_t capacity)
{
    uintptr_t a=(uintptr_t)buffer;
    if(!clear_wait(CMD,START))return TIKU_IPC_BUSY;
    stage=buffer;stage_capacity=capacity;stage_length=length;stage_write=write;
    seen_raw=seen_dma=0;
    memset(&descriptor,0,sizeof descriptor);
    /* The chained second address terminates this single-descriptor transfer. */
    descriptor.control=OWN|(1u<<4)|(1u<<3)|(1u<<2);
    /* IDMAC spans whole words; the card byte count retains the requested length. */
    descriptor.size=(length+3u)&~3u;descriptor.address=(uint32_t)a;
    tiku_cpu_dcache_clean(buffer,capacity);
    if(!write)tiku_cpu_dcache_invalidate(buffer,capacity);
    tiku_cpu_dcache_clean(&descriptor,sizeof descriptor);
    TIKU_P4_REG_WRITE(RINT,UINT32_MAX);TIKU_P4_REG_WRITE(IDSTS,UINT32_MAX);
    TIKU_P4_REG_WRITE(REG(0x1c),length);TIKU_P4_REG_WRITE(REG(0x20),length);
    TIKU_P4_REG_WRITE(REG(0x88),(uint32_t)(uintptr_t)&descriptor);
    bits(CTRL,(1u<<25)|(1u<<5),(1u<<25)|(1u<<5));TIKU_P4_REG_WRITE(BMOD,(1u<<7)|(1u<<1));
    TIKU_P4_REG_WRITE(REG(0x84),1);
    TIKU_P4_REG_WRITE(REG(0x28),(write?1u<<31:0)|((uint32_t)function<<28)|
        ((uint32_t)increment<<26)|(address<<9)|(length&511u));
    state.active=1;state.commands++;
    TIKU_P4_REG_WRITE(CMD,START|SLOT|WAIT_DATA|(1u<<6)|(1u<<8)|(1u<<9)|
                        (write?1u<<10:0)|53u);
    return TIKU_IPC_OK;
}
tiku_ipc_err_t tiku_p4_sdmmc_start(uint8_t write,uint8_t function,uint32_t address,
    uint8_t increment,void *buffer,uint16_t length,uint16_t capacity)
{
    uintptr_t a=(uintptr_t)buffer;
    if(!state.opened||state.active||write>1||function>7||address>0x1ffffu||increment>1||
        !buffer||!length||length>512||length>capacity||capacity>512||!capacity||
        (capacity%64u)||(a%64u)||a<0x4ff00000u||a>=0x4ffa0000u||
        capacity>0x4ffa0000u-a||(increment&&length-1u>0x1ffffu-address))return TIKU_IPC_INVALID;
    memset(&follow,0,sizeof follow);
    if(length>=4&&(length&3u)){
        follow.buffer=buffer;follow.prefix=length&~3u;follow.tail=length&3u;
        follow.address=address+(increment?follow.prefix:0);
        follow.function=function;follow.increment=increment;follow.write=write;
        length=follow.prefix;
    }
    tiku_ipc_err_t result=start_chunk(write,function,address,increment,buffer,length,capacity);
    if(result)memset(&follow,0,sizeof follow);
    return result;
}

tiku_ipc_err_t tiku_p4_sdmmc_poll(void)
{
    if(!state.active)return TIKU_IPC_INVALID;
    seen_raw|=TIKU_P4_REG_READ(RINT);seen_dma|=TIKU_P4_REG_READ(IDSTS);
    state.raw=seen_raw;state.dma=seen_dma;
    if((seen_raw&CMD_ERRORS)||(seen_dma&DMA_ERRORS)||
        ((seen_raw&(1u<<2))&&(TIKU_P4_REG_READ(REG(0x30))&0xcb00u))){
        state.errors++;return TIKU_IPC_IO;
    }
    if((seen_raw&12u)!=12u||!(seen_dma&(stage_write?1u:2u)))return TIKU_IPC_AGAIN;
    tiku_cpu_dcache_invalidate(&descriptor,sizeof descriptor);
    if(descriptor.control&OWN)return TIKU_IPC_AGAIN;
    if(!stage_write)tiku_cpu_dcache_invalidate(stage,stage_capacity);
    state.bytes+=stage_length;
    if(follow.tail){
        uint8_t length=follow.tail;follow.tail=0;
        memset(tail_stage,0,sizeof tail_stage);
        if(follow.write)TIKU_P4_SDMMC_COPY(tail_stage,follow.buffer+follow.prefix,length);
        else follow.copy_out=length;
        tiku_ipc_err_t result=start_chunk(follow.write,follow.function,follow.address,
            follow.increment,tail_stage,length,sizeof tail_stage);
        return result?result:TIKU_IPC_AGAIN;
    }
    if(follow.copy_out)TIKU_P4_SDMMC_COPY(follow.buffer+follow.prefix,tail_stage,follow.copy_out);
    memset(&follow,0,sizeof follow);
    state.active=0;stage=NULL;
    return TIKU_IPC_OK;
}

uint8_t tiku_p4_sdmmc_abort(void)
{
    if(!state.opened)return 1;
    bits(BMOD,1u<<7,0);
    bits(CTRL,7u|(1u<<25)|(1u<<5),7u);
    bits(BMOD,1u,1u);
    if(!clear_wait(CTRL,7u)||!clear_wait(BMOD,1u)||
        (TIKU_P4_REG_READ(IDSTS)&(15u<<13)))return 0;
    memset(&follow,0,sizeof follow);
    state.active=0;stage=NULL;return 1;
}

void tiku_p4_sdmmc_peer_enable(uint8_t enabled)
{
    TIKU_P4_REG_WRITE(enabled?0x500e0014u:0x500e0018u,BIT(54));
    TIKU_P4_REG_WRITE(0x500e0030u,BIT(54));
}

void tiku_p4_sdmmc_peer_recovery(uint8_t recovery)
{
    TIKU_P4_REG_WRITE(0x500e0028u,BIT(6));
    bits(0x5012b020u,1u<<4,0);
    TIKU_P4_REG_WRITE(PIN_OUT(6),256u);
    bits(PIN_CFG(6),(1u<<2)|(7u<<7)|(31u<<13),0);
    bits(PIN_MUX(6),(7u<<12)|(1u<<7)|(1u<<8),(1u<<12)|(1u<<9));
    TIKU_P4_REG_WRITE(recovery?0x500e0008u:0x500e000cu,BIT(6));
    TIKU_P4_REG_WRITE(0x500e0024u,BIT(6));
}

tiku_ipc_err_t tiku_p4_sdmmc_close(void)
{
    if(!state.opened)return TIKU_IPC_OK;
    if(!tiku_p4_sdmmc_abort())return TIKU_IPC_BUSY;
    TIKU_P4_REG_WRITE(REG(0x10),0);(void)update_clock();
    pads_close();
    bits(RESET,RESET_BIT,saved_clock[3]);bits(PHASE_CLOCK,PHASE_BITS,saved_clock[2]);
    bits(CORE_CLOCK,CORE_BITS,saved_clock[1]);bits(SYS_CLOCK,SYS_BIT,saved_clock[0]);
    state.opened=0;return TIKU_IPC_OK;
}
void tiku_p4_sdmmc_status(tiku_p4_sdmmc_status_t *out)
{ if(out)*out=state; }
