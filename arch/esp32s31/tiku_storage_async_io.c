/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_storage_async_io.c - Automatic flash suspension with caller-owned controller state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_storage_async.h"
#include "tiku_runtime_arch.h"
#include <kernel/memory/tiku_nvm_meter.h>
#include <kernel/cpu/tiku_watchdog.h>
#if TIKU_S31_STORAGE_ASYNC_ENABLE
#define SPI1 0x20501000u
#ifndef TIKU_S31_FLASH_REG
#define TIKU_S31_FLASH_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define REG(off) TIKU_S31_FLASH_REG(SPI1 + (off))
static tiku_s31_flash_t *active_flash;
static unsigned profile_valid;
static const uint32_t saved_registers[15]={
    SPI1+4,SPI1+8,SPI1+0xc,SPI1+0x14,SPI1+0x18,SPI1+0x1c,SPI1+0x20,
    SPI1+0x24,SPI1+0x28,SPI1+0x98,SPI1+0x9c,SPI1+0xa0,SPI1+0xa4,0x20500010u,0x20500054u
};
/** @brief Read the wrapping microsecond clock. */
static uint32_t flash_time(void) { return (uint32_t)(tiku_s31_time() >> 4); }
/** @brief Send a short command or launch a hardware-managed program/erase without waiting. */
static uint32_t command(unsigned code, uint32_t address, unsigned address_bits,
                         unsigned dummy, const uint32_t *data, unsigned out, unsigned in)
{
    if (REG(0)) tiku_s31_fatal("flash command occupied");
    REG(8)=0;
    REG(0x18)=(1u<<31) | (address_bits ? 1u<<30 : 0) | (dummy ? 1u<<29 : 0) |
              (in ? 1u<<28 : 0) | (out ? 1u<<27 : 0);
    REG(0x1c)=(address_bits ? (address_bits-1u)<<26 : 0) | (dummy ? dummy-1u : 0);
    REG(0x20)=(7u<<28)|code; REG(4)=address;
    REG(0x24)=out ? out*8u-1u : 0; REG(0x28)=in ? in*8u-1u : 0;
    for (unsigned i=0;i<out/4u;i++) REG(0x58+4u*i)=data[i];
    int write=code==0x20 || code==2;
    REG(0)=(1u<<18) | (write ? 1u<<17 : 0);
    if(write)return 0;
    uint32_t start=flash_time();
    while (REG(0)) if ((uint32_t)(flash_time()-start)>=100u) tiku_s31_fatal("flash command timeout");
    return REG(0x58);
}
/** @brief Require the recorded XMC ID and an SFDP suspend profile within the slice timing. */
static int flash_profile(void)
{
    if ((command(0x9f,0,0,0,NULL,0,3)&0xffffffu)!=0x184046u) return -1;
    if (command(0x5a,0,24,8,NULL,0,8)!=0x50444653u) return -1;
    uint32_t header=REG(0x5c);
    unsigned count=((header>>16)&255u)+1u;
    if (((header>>8)&255u)!=1u || count>8u) return -1;
    for (unsigned i=0;i<count;i++) {
        uint32_t h=command(0x5a,8u+8u*i,24,8,NULL,0,8), p=REG(0x5c);
        if ((h&255u) || (p>>24)!=255u || ((h>>16)&255u)!=1u || (h>>24)<13u) continue;
        p &= 0xffffffu;
        if (p>0xffffffu-52u) return -1;
        uint32_t timing=command(0x5a,p+44u,24,8,NULL,0,8);
        if (REG(0x5c)!=0x757a757au || (timing&0x80000000u) || (timing&255u)!=0xccu) return -1;
        unsigned pu=(timing>>18)&3u, eu=(timing>>29)&3u;
        unsigned pm=((timing>>13)&31u)+1u, em=((timing>>24)&31u)+1u;
        pm *= pu<2 ? 1u : pu==2 ? 8u : 64u;
        em *= eu<2 ? 1u : eu==2 ? 8u : 64u;
        if (pm>50u || em>50u || (((timing>>9)&15u)+1u)*64u>64u ||
            (((timing>>20)&15u)+1u)*64u>512u) return -1;
        return 0;
    }
    return -1;
}
/** @brief Restore controller configuration after idle or a refused command. */
static int flash_retire(tiku_s31_flash_t *op,int result)
{
    for(unsigned i=0;i<15;i++)TIKU_S31_FLASH_REG(saved_registers[i])=op->saved[i];
    op->operation=0;active_flash=NULL;return result;
}
/** @brief Set a <=20 MHz clock and the admitted program/erase suspend/resume delays. */
static int flash_configure(unsigned operation)
{
    uint32_t root=TIKU_S31_FLASH_REG(0x20587064u);
    unsigned source=(root>>5)&3u;
    if(source==3)return -1;
    unsigned mhz=source==0 ? 40u : source==1 ? 480u : 320u;
    unsigned divider=((root>>9)&255u)+1u;
    mhz=(mhz+divider-1u)/divider;
    if(mhz<10u)return -1;
    unsigned div=(mhz+19u)/20u;
    REG(0x14)=div==1 ? 1u<<31 : (div-1u)|((div-1u)/2u<<8)|((div-1u)<<16);
    if(!operation)return 0;
    unsigned resume=operation==2 ? 10u : 80u;
    REG(0xc)=(REG(0xc)&~0x3ffffcu)|(8u<<2)|(resume<<12);
    REG(0x98)=(5u<<16)|1u;
    REG(0xa0)=(0x35u<<16)|0x75u;
    REG(0xa4)=(0x7au<<16)|(1u<<7)|(1u<<6)|(1u<<5);
    REG(0x9c)=(5u<<25)|(1u<<24)|(1u<<23)|(128u<<6)|(1u<<5)|(1u<<3)|(1u<<2);
    /* Bound CS-high spacing at the flash source clock; SPI0 keeps its mapped clock. */
    unsigned hold=(mhz*30u+999u)/1000u;
    TIKU_S31_FLASH_REG(0x20500010u)=(TIKU_S31_FLASH_REG(0x20500010u)&~(63u<<25))|(hold<<25);
    TIKU_S31_FLASH_REG(0x20500054u)=(TIKU_S31_FLASH_REG(0x20500054u)&~(4095u<<7))|(31u<<7);
    return 0;
}
int tiku_s31_storage_io_busy(void) { return active_flash!=NULL; }
int tiku_s31_storage_slice(tiku_s31_flash_t *op,unsigned operation,uint32_t address,
                            void *buffer,size_t length,int resume)
{
    uintptr_t ptr=(uintptr_t)buffer;
    if(!op || !tiku_s31_storage_context() || operation<1 || operation>2 ||
       address<TIKU_S31_STORAGE_START || address>=TIKU_S31_STORAGE_END ||
       length>TIKU_S31_STORAGE_END-address ||
       (operation==1 ? (address&4095u) || length :
        !buffer || !length || length>64 || ((ptr|address|length)&3u) || (address&255u)+length>256)) return -1;
    if(resume) {
        if(op!=active_flash || op->operation!=operation || op->address!=address)return -1;
        if((uint32_t)(flash_time()-op->started)>(operation==1 ? 5000000u : 100000u))
            tiku_s31_fatal("flash progress timeout");
        if(REG(0xa4)&1u) {REG(0x9c)|=1u;return 1;}
        if(REG(0))return 1;
        uint32_t sr=command(5,0,0,0,NULL,0,1),sr2=command(0x35,0,0,0,NULL,0,1);
        if((sr&1u) || (sr2&128u))return 1;
        tiku_watchdog_kick();
        return flash_retire(op,(sr&2u) ? -1 : 0);
    }
    if(active_flash || op->operation)return -1;
    if(REG(0))return -1;
    for(unsigned i=0;i<15;i++)op->saved[i]=TIKU_S31_FLASH_REG(saved_registers[i]);
    active_flash=op;
    if(flash_configure(0))return flash_retire(op,-1);
    if(!profile_valid) {
        if(flash_profile())return flash_retire(op,-1);
        profile_valid=1;
    }
    uint32_t sr=command(5,0,0,0,NULL,0,1),sr2=command(0x35,0,0,0,NULL,0,1);
    if((sr&1u) || (sr2&128u))tiku_s31_fatal("flash foreign operation");
    if((sr&0x7cu) || (sr2&0x40u) || flash_configure(operation))return flash_retire(op,-1);
    command(6,0,0,0,NULL,0,0);
    if(!(command(5,0,0,0,NULL,0,1)&2u))return flash_retire(op,-1);
    op->operation=operation;op->address=address;op->started=flash_time();
    tiku_nvm_mark_t mark=tiku_nvm_meter_begin();
    command(operation==1 ? 0x20 : 2,address,24,0,buffer,(unsigned)length,0);
    tiku_nvm_meter_end(mark,operation==2 ? length : 0,operation==2,operation==1);
    return 1;
}
#endif
