/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdcard_arch.c - Slot0 bounded block transfers with an exclusive host lease.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sdcard_arch.h"
#include "tiku_sdmmc_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_systimer_arch.h"
#include <string.h>
#include <hal/tiku_cpu.h>

#define REG(n) (0x50083000u+(n))
#define CTRL REG(0)
#define CMD REG(0x2c)
#define RINT REG(0x44)
#define STATUS REG(0x48)
#define START (1u<<31)
#define UPDATE (1u<<21)
#define ERRORS 0xbfc2u
#define R1_ERRORS 0xfdffe008u
#define MUX(p) (0x500e1004u+4u*(p))
#define CFG(p) (0x500e0074u+4u*(p))
#define OUT(p) (0x500e0558u+4u*(p))
#define INPUT(s) (0x500e0158u+4u*(s))
#define LDO 0x501151d8u
#define LDO_ANA 0x501151dcu

static const uint8_t offsets[]={0,8,12,16,20,24,28,32,36,76,128,136,144};
static uint32_t saved_regs[sizeof offsets], saved_ldo[2], saved_inputs[2];
static struct { uint32_t mux,cfg,out,enabled,level; } pads[7];
static struct {
    uint32_t control,size,address,next,padding[12];
} card_descriptor __attribute__((section(".p4.ipc"),aligned(64)));
static uint8_t card_stage[4096] __attribute__((section(".p4.ipc"),aligned(64)));
static tiku_p4_sdcard_t *owner;
static uint8_t claimed, configured, selected, writable;
static uint64_t write_first,write_end;

/** @brief Replace a register field. */
static void field(uint32_t address,uint32_t mask,uint32_t value)
{ TIKU_P4_REG_WRITE(address,(TIKU_P4_REG_READ(address)&~mask)|(value&mask)); }
/** @brief Wait for a field to clear, with a timer and iteration bound. */
static int wait_clear(uint32_t address,uint32_t mask,uint32_t us)
{
    uint64_t begin,now;
    if(tiku_p4_systimer_read(&begin))return -1;
    for(unsigned n=0;n<2000000u;n++){
        if(!(TIKU_P4_REG_READ(address)&mask))return 0;
        if(tiku_p4_systimer_read(&now)||((now-begin)&TIKU_P4_SYSTIMER_MASK)>16ull*us)break;
    }
    return -1;
}
/** @brief Wait for card power or initialization clocks without dispatching processes. */
static int delay(uint32_t us)
{
    uint64_t begin,now;
    if(tiku_p4_systimer_read(&begin))return -1;
    for(unsigned n=0;n<2000000u;n++){
        if(tiku_p4_systimer_read(&now))return -1;
        if(((now-begin)&TIKU_P4_SYSTIMER_MASK)>=16ull*us)return 0;
    }
    return -1;
}
/** @brief Latch the slot0 clock fields without resetting the controller. */
static int clock_update(void)
{
    TIKU_P4_REG_WRITE(RINT,UINT32_MAX);
    TIKU_P4_REG_WRITE(CMD,START|UPDATE|(1u<<13));
    return wait_clear(CMD,START,3000)||(TIKU_P4_REG_READ(RINT)&(1u<<12));
}
/** @brief Execute an allowed command with up to eight sectors in an owned SRAM stage. */
static tiku_ipc_err_t transfer(uint8_t index,uint32_t argument,uint32_t response[4],void *data,unsigned blocks)
{
    uint32_t flags=START|(1u<<13),raw=0,dma=0;
    unsigned writing=index==24||index==25,multi=index==18||index==25;
    uint32_t done=4u|(data?8u:0)|(multi?1u<<14:0);
    uint64_t begin,now;
    if(!claimed||!owner||!response)return TIKU_IPC_INVALID;
    switch(index){
    case 0: if(argument||data)return TIKU_IPC_INVALID;flags|=1u<<15;break;
    case 2: case 9: flags|=(1u<<6)|(1u<<7)|(1u<<8);break;
    case 41: flags|=1u<<6;break;
    case 3: case 7: case 8: case 13: case 55: flags|=(1u<<6)|(1u<<8);break;
    case 17: case 18: case 24: case 25:
        if(writing&&!writable)return TIKU_IPC_UNSUPPORTED;
        if(!selected||!data||!blocks||blocks>8||(uint64_t)argument>=owner->sectors||
           blocks>owner->sectors-argument||(multi!=(blocks>1)))return TIKU_IPC_INVALID;
        if(writing&&((uint64_t)argument<write_first||blocks>write_end-argument||
           (uint64_t)argument>=write_end))return TIKU_IPC_INVALID;
        flags|=(1u<<6)|(1u<<8)|(1u<<9)|(writing?1u<<10:0)|(multi?1u<<12:0);break;
    default:return TIKU_IPC_UNSUPPORTED;
    }
    if(index!=17&&index!=18&&index!=24&&index!=25&&data)return TIKU_IPC_INVALID;
    if(wait_clear(CMD,START,3000))return TIKU_IPC_TIMEOUT;
    if(data){
        field(CTRL,6u,6u);field(REG(0x80),1u,1u);
        if(wait_clear(CTRL,6u,3000)||wait_clear(REG(0x80),1u,3000))return TIKU_IPC_TIMEOUT;
        memset(&card_descriptor,0,sizeof card_descriptor);
        card_descriptor.control=(1u<<31)|(1u<<4)|(1u<<3)|(1u<<2);
        card_descriptor.size=blocks*512;card_descriptor.address=(uint32_t)(uintptr_t)card_stage;
        if(writing)memcpy(card_stage,data,blocks*512);
        tiku_cpu_dcache_clean(card_stage,sizeof card_stage);
        if(!writing)tiku_cpu_dcache_invalidate(card_stage,sizeof card_stage);
        tiku_cpu_dcache_clean(&card_descriptor,sizeof card_descriptor);
        TIKU_P4_REG_WRITE(REG(0x8c),UINT32_MAX);
        TIKU_P4_REG_WRITE(REG(0x1c),512);TIKU_P4_REG_WRITE(REG(0x20),blocks*512);
        TIKU_P4_REG_WRITE(REG(0x88),(uint32_t)(uintptr_t)&card_descriptor);
        field(CTRL,(1u<<25)|(1u<<5),(1u<<25)|(1u<<5));
        TIKU_P4_REG_WRITE(REG(0x80),(1u<<7)|(1u<<1));
        TIKU_P4_REG_WRITE(REG(0x84),1);
    }
    if(tiku_p4_systimer_read(&begin))return TIKU_IPC_IO;
    memset(response,0,16);owner->command=index;
    TIKU_P4_REG_WRITE(RINT,UINT32_MAX);
    TIKU_P4_REG_WRITE(REG(0x28),argument);
    TIKU_P4_REG_WRITE(CMD,flags|index);
    for(unsigned n=0;n<2000000u;n++){
        raw=TIKU_P4_REG_READ(RINT);
        if(raw&ERRORS)break;
        if(data){
            dma=TIKU_P4_REG_READ(REG(0x8c));
            if(dma&0x34u)break;
            if((raw&done)==done&&(dma&(writing?1u:2u))){
                tiku_cpu_dcache_invalidate(&card_descriptor,sizeof card_descriptor);
                if(!(card_descriptor.control&(1u<<31)))break;
            }
        }else if(raw&4u)break;
        if(tiku_p4_systimer_read(&now)||((now-begin)&TIKU_P4_SYSTIMER_MASK)>16000000u)break;
    }
    for(unsigned n=0;n<4;n++)response[n]=TIKU_P4_REG_READ(REG(0x30+4*n));
    owner->raw=raw;owner->response=response[0];
    owner->status=TIKU_P4_REG_READ(STATUS);owner->dma=dma;
    owner->transferred=TIKU_P4_REG_READ(REG(0x5c));
    if(!tiku_p4_systimer_read(&now))owner->elapsed_us=(uint32_t)(((now-begin)&TIKU_P4_SYSTIMER_MASK)/16u);
    if((raw&ERRORS)||(raw&done)!=done||
       (data&&((dma&0x34u)||!(dma&(writing?1u:2u))||(card_descriptor.control&(1u<<31))))||wait_clear(CMD,START,3000))return TIKU_IPC_IO;
    if((index==7||index==13||index==17||index==18||index==24||index==25||index==55)&&(response[0]&R1_ERRORS))return TIKU_IPC_IO;
    if(data&&!writing){
        tiku_cpu_dcache_invalidate(card_stage,sizeof card_stage);
        memcpy(data,card_stage,blocks*512);
    }
    return TIKU_IPC_OK;
}
/** @brief Execute one non-data command or one read sector. */
static tiku_ipc_err_t command(uint8_t index,uint32_t argument,uint32_t response[4],void *data)
{ return transfer(index,argument,response,data,data?1:0); }
/** @brief Save slot0 resources and enable its 3.3 V supply, pads and 400 kHz clock. */
static int configure(void)
{
    for(unsigned n=0;n<sizeof offsets;n++)saved_regs[n]=TIKU_P4_REG_READ(REG(offsets[n]));
    saved_ldo[0]=TIKU_P4_REG_READ(LDO);saved_ldo[1]=TIKU_P4_REG_READ(LDO_ANA);
    saved_inputs[0]=TIKU_P4_REG_READ(INPUT(126));saved_inputs[1]=TIKU_P4_REG_READ(INPUT(130));
    for(unsigned i=0;i<7;i++){
        unsigned p=39+i;
        pads[i].mux=TIKU_P4_REG_READ(MUX(p));pads[i].cfg=TIKU_P4_REG_READ(CFG(p));
        pads[i].out=TIKU_P4_REG_READ(OUT(p));
        pads[i].enabled=TIKU_P4_REG_READ(0x500e002cu)&(1u<<(p-32));
        pads[i].level=TIKU_P4_REG_READ(0x500e0010u)&(1u<<(p-32));
    }
    configured=1;
    field(LDO_ANA,1u<<27,1u<<27);
    field(LDO,(1u<<7)|(1u<<8)|(7u<<9)|(1u<<14),(1u<<7)|(1u<<8)|(1u<<14));
    field(LDO_ANA,(1u<<26)|(1u<<27),1u<<26);
    /* GPIO45 low enables the card's AO3401 power switch. */
    TIKU_P4_REG_WRITE(0x500e0018u,1u<<13);
    TIKU_P4_REG_WRITE(OUT(45),256);field(CFG(45),(1u<<2)|(7u<<7)|(31u<<13),0);
    field(MUX(45),(7u<<12)|(1u<<7)|(1u<<8)|(1u<<9),1u<<12);
    TIKU_P4_REG_WRITE(0x500e0030u,1u<<13);
    if(delay(5000))return -1;
    for(unsigned p=39;p<=44;p++){
        TIKU_P4_REG_WRITE(0x500e0034u,1u<<(p-32));
        field(CFG(p),(1u<<2)|(7u<<7)|(31u<<13),0);
        field(MUX(p),(7u<<12)|(1u<<7)|(1u<<8)|(1u<<9),(1u<<9)|(p==43?0:1u<<8));
    }
    TIKU_P4_REG_WRITE(INPUT(126),(1u<<7)|0x3cu);
    TIKU_P4_REG_WRITE(INPUT(130),(1u<<7)|0x3cu);
    field(CTRL,(1u<<25)|(1u<<5),0);field(REG(0x80),1u<<7,0);
    TIKU_P4_REG_WRITE(REG(0x24),0);TIKU_P4_REG_WRITE(REG(0x90),0x337u);
    TIKU_P4_REG_WRITE(REG(0x14),(40000u<<8)|255u);
    TIKU_P4_REG_WRITE(REG(0x4c),(3u<<28)|(15u<<16)|16u);
    field(REG(0x18),(1u<<16)|1u,0);
    field(REG(0x10),0x10001u,0);
    if(clock_update())return -1;
    field(REG(0x08),255u,50u);field(REG(0x0c),3u,0);
    if(clock_update())return -1;
    field(REG(0x10),1u,1u);
    return clock_update()||delay(1000);
}

tiku_ipc_err_t tiku_p4_sdcard_open(tiku_p4_sdcard_t *card)
{
    uint32_t r[4];tiku_ipc_err_t result;
    if(!card)return TIKU_IPC_INVALID;
    if(claimed)return TIKU_IPC_BUSY;
    memset(card,0,sizeof *card);
    result=tiku_p4_sdmmc_card_claim();if(result)return result;
    claimed=1;selected=writable=0;owner=card;
    if(configure())return TIKU_IPC_IO;
    result=command(0,0,r,NULL);if(result)return result;
    result=command(8,0x1aau,r,NULL);if(result)return result;
    if((r[0]&0xfffu)!=0x1aau)return TIKU_IPC_UNSUPPORTED;
    uint64_t begin,now;
    if(tiku_p4_systimer_read(&begin))return TIKU_IPC_IO;
    for(unsigned n=0;n<1000;n++){
        result=command(55,0,r,NULL);if(result)return result;
        if(!(r[0]&(1u<<5)))return TIKU_IPC_IO;
        result=command(41,0x40ff8000u,r,NULL);if(result)return result;
        card->ocr=r[0];if(r[0]&(1u<<31))break;
        if(delay(1000)||tiku_p4_systimer_read(&now)||
           ((now-begin)&TIKU_P4_SYSTIMER_MASK)>16000000u)return TIKU_IPC_TIMEOUT;
    }
    if(!(card->ocr&(1u<<31)))return TIKU_IPC_TIMEOUT;
    if(!(card->ocr&(1u<<30)))return TIKU_IPC_UNSUPPORTED;
    result=command(2,0,card->cid,NULL);if(result)return result;
    result=command(3,0,r,NULL);if(result)return result;
    card->rca=r[0]&0xffff0000u;
    if(!card->rca||(r[0]&0xe000u))return TIKU_IPC_IO;
    result=command(9,card->rca,card->csd,NULL);if(result)return result;
    if(card->csd[3]>>30!=1)return TIKU_IPC_UNSUPPORTED;
    uint32_t csize=((card->csd[2]&63u)<<16)|(card->csd[1]>>16);
    card->sectors=((uint64_t)csize+1)*1024u;
    result=command(7,card->rca,r,NULL);if(result)return result;
    if(wait_clear(STATUS,1u<<9,200000))return TIKU_IPC_TIMEOUT;
    result=command(13,card->rca,r,NULL);if(result)return result;
    if((r[0]&0x1f00u)!=0x900u)return TIKU_IPC_IO;
    selected=1;return TIKU_IPC_OK;
}

int tiku_p4_sdcard_read(uint32_t lba,void *buffer,void *context)
{
    uint32_t r[4];
    if(!selected||!buffer||!context||context!=owner)return TIKU_IPC_INVALID;
    tiku_ipc_err_t result=command(17,lba,r,buffer);
    if(!result)owner->reads++;
    return result;
}

tiku_ipc_err_t tiku_p4_sdcard_close(void)
{
    if(!claimed)return TIKU_IPC_OK;
    selected=writable=0;
    if(configured){
        /* A live command or data engine prevents lease release and clock restoration. */
        if(wait_clear(CMD,START,200000)||wait_clear(STATUS,(1u<<10)|(1u<<9),1000000))return TIKU_IPC_BUSY;
        field(REG(0x80),1u<<7,0);field(CTRL,(1u<<25)|(1u<<5),0);
        field(CTRL,6u,6u);field(REG(0x80),1u,1u);
        if(wait_clear(CTRL,6u,3000)||wait_clear(REG(0x80),1u,3000)||
           (TIKU_P4_REG_READ(REG(0x8c))&(15u<<13)))return TIKU_IPC_BUSY;
        field(REG(0x10),1u,0);if(clock_update())return TIKU_IPC_BUSY;
        TIKU_P4_REG_WRITE(REG(8),saved_regs[1]);TIKU_P4_REG_WRITE(REG(12),saved_regs[2]);
        if(clock_update())return TIKU_IPC_BUSY;
        TIKU_P4_REG_WRITE(REG(16),saved_regs[3]);if(clock_update())return TIKU_IPC_BUSY;
        for(unsigned n=0;n<sizeof offsets;n++)
            if(offsets[n]!=0&&offsets[n]!=8&&offsets[n]!=12&&offsets[n]!=16&&offsets[n]!=128)
                TIKU_P4_REG_WRITE(REG(offsets[n]),saved_regs[n]);
        TIKU_P4_REG_WRITE(REG(0x80),saved_regs[10]);
        TIKU_P4_REG_WRITE(CTRL,saved_regs[0]);
        TIKU_P4_REG_WRITE(RINT,UINT32_MAX);
        TIKU_P4_REG_WRITE(INPUT(126),saved_inputs[0]);TIKU_P4_REG_WRITE(INPUT(130),saved_inputs[1]);
        for(unsigned i=0;i<7;i++){
            unsigned p=39+i;uint32_t bit=1u<<(p-32);
            TIKU_P4_REG_WRITE(0x500e0034u,bit);
            TIKU_P4_REG_WRITE(pads[i].level?0x500e0014u:0x500e0018u,bit);
            TIKU_P4_REG_WRITE(CFG(p),pads[i].cfg);TIKU_P4_REG_WRITE(OUT(p),pads[i].out);
            TIKU_P4_REG_WRITE(MUX(p),pads[i].mux);
            if(pads[i].enabled)TIKU_P4_REG_WRITE(0x500e0030u,bit);
        }
        TIKU_P4_REG_WRITE(LDO,saved_ldo[0]);TIKU_P4_REG_WRITE(LDO_ANA,saved_ldo[1]);
    }
    claimed=configured=selected=0;owner=NULL;
    tiku_p4_sdmmc_card_release();return TIKU_IPC_OK;
}

int tiku_p4_sdcard_write_enable(tiku_p4_sdcard_t *card,uint64_t first,uint64_t sectors)
{
    if(!selected||card!=owner||!sectors||first>=card->sectors||sectors>card->sectors-first)
        return TIKU_IPC_INVALID;
    write_first=first;write_end=first+sectors;writable=1;return 0;
}
int tiku_p4_sdcard_sync(tiku_p4_sdcard_t *card)
{
    uint32_t r[4];
    if(!selected||card!=owner)return TIKU_IPC_INVALID;
    if(wait_clear(STATUS,1u<<9,1000000))return TIKU_IPC_TIMEOUT;
    for(unsigned i=0;i<1000;i++){
        int rc=command(13,card->rca,r,NULL);if(rc)return rc;
        if((r[0]&0x1f00u)==0x900u)return 0;
        if(delay(1000))return TIKU_IPC_TIMEOUT;
    }
    return TIKU_IPC_TIMEOUT;
}
int tiku_p4_sdcard_blocks(tiku_p4_sdcard_t *card,uint8_t write,uint32_t lba,
                         uint32_t count,void *data)
{
    uint32_t r[4];
    if(!selected||card!=owner||write>1||!data||!count||count>8)return TIKU_IPC_INVALID;
    int rc=transfer(write?(count==1?24:25):(count==1?17:18),lba,r,data,count);
    if(!rc&&write)rc=tiku_p4_sdcard_sync(card);
    if(!rc&&!write&&count>1)rc=tiku_p4_sdcard_sync(card);
    if(rc){writable=0;return rc;}
    if(write)card->writes+=count;else card->reads+=count;
    return 0;
}
int tiku_p4_sdcard_clock(tiku_p4_sdcard_t *card,uint32_t hz)
{
    if(!selected||card!=owner||hz<400000||hz>20000000)return TIKU_IPC_INVALID;
    if(tiku_p4_sdcard_sync(card))return TIKU_IPC_IO;
    uint32_t divider=(40000000u+2*hz-1)/(2*hz);
    /* The software deadline bounds data waits across the selected clock range. */
    TIKU_P4_REG_WRITE(REG(0x14),UINT32_MAX);
    field(REG(0x10),1u,0);if(clock_update())return TIKU_IPC_IO;
    field(REG(8),255u,divider);if(clock_update())return TIKU_IPC_IO;
    field(REG(0x10),1u,1u);return clock_update()?TIKU_IPC_IO:0;
}
