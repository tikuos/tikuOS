/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_meter_arch.c - PCNT observation of the wired SDIO clock pad.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include "tiku_sdio_arch.h"
#define PCNT 0x60012000u
#define PCR_PCNT 0x60096094u
#define CLOCK_INPUT 0x600912e8u
uint32_t tiku_c6_sdio_clock_khz(void)
{
    uint32_t clock=C6_REG(PCR_PCNT),control=C6_REG(PCNT+0x60),irq=C6_REG(PCNT+0x48);
    uint32_t config=C6_REG(PCNT),limits=C6_REG(PCNT+8),input=C6_REG(CLOCK_INPUT);
    uint32_t state,count=0;uint64_t began,ended;
    /* Unit0 must be held reset, with its interrupt disabled, before borrowing. */
    if(!(control&1u)||(irq&1u))return 0;
    state=native_irq_save();C6_REG(PCR_PCNT)=(clock|1u)&~2u;
    C6_REG(PCNT+0x60)=control|(1u<<16)|3u;
    C6_REG(PCNT)=1u<<18;C6_REG(PCNT+8)=0x80007fffu;
    C6_REG(CLOCK_INPUT)=(1u<<7)|19u;
    began=native_time();C6_REG(PCNT+0x60)=(control|(1u<<16))&~3u;
    ended=began;
    for(unsigned n=0;n<200000;n++){
        ended=native_time();if(ended-began>=16000u)break;
    }
    C6_REG(PCNT+0x60)|=2u;count=C6_REG(PCNT+0x30)&0xffffu;
    C6_REG(PCNT+0x60)|=1u;
    C6_REG(CLOCK_INPUT)=input;C6_REG(PCNT)=config;C6_REG(PCNT+8)=limits;
    C6_REG(PCNT+0x60)=control;C6_REG(PCR_PCNT)=clock;
    native_irq_restore(state);
    if(ended<=began||ended-began>32000u||count>=32767u)return 0;
    return (uint32_t)((uint64_t)count*16000u/(ended-began));
}
