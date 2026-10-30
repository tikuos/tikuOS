/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - C6 SAR completion checks and bounded RF entropy reads.
 *
 * Register sequence: ESP-IDF 4d59230 bootloader_random_esp32c6.c,
 * C6 adc_ll.h, rng_ll.h and regi2c_impl.c (Apache-2.0).
 *
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trng_arch.h"
#include "tiku_native_arch.h"
#include <hal/tiku_mem_hal.h>
#define SAR 0x6000e000u
#define ANA 0x600af800u
#define RNG 0x600b2808u
static const uint32_t saved_regs[]={0x60096080u,0x60096084u,0x600af018u,
    0x600b0154u,ANA+0x1c,0x600b2800u,SAR,SAR+4,SAR+0x18,SAR+0x1c};
static const uint8_t analog_regs[]={0,1,3,4,7};
static tiku_c6_trng_radio_t radio_source;

void tiku_c6_trng_radio_set(tiku_c6_trng_radio_t source)
{
    uint32_t state=native_irq_save();radio_source=source;native_irq_restore(state);
}

/** @brief Replace a register field, preserving neighboring bits. */
static void field(uint32_t at,uint32_t mask,uint32_t value)
{C6_REG(at)=(C6_REG(at)&~mask)|value;}
/** @brief Wait for an internal analog-bus command within a bounded poll count. */
static int idle(uint32_t at)
{
    for(unsigned n=0;n<200000;n++)if(!(C6_REG(at)&(1u<<25)))return 0;
    return -1;
}
/** @brief Transfer one SAR register without changing the other analog slaves. */
static int analog(uint8_t reg,uint8_t *value,int write)
{
    uint32_t at=ANA+((C6_REG(ANA+0x20)&(1u<<11))?0:4);
    if(idle(at))return -1;
    C6_REG(ANA+0x1c)=0xffffffu&~(1u<<9);
    C6_REG(at)=0x69u|((uint32_t)reg<<8)|(write?((1u<<24)|((uint32_t)*value<<16)):0);
    if(idle(at))return -1;
    if(!write)*value=(uint8_t)(C6_REG(at)>>16);
    return 0;
}
/** @brief Delay against the already-running 16 MHz SYSTIMER. */
static void delay(uint32_t us)
{
    uint64_t start=native_time();while(native_time()-start<(uint64_t)us*16u){}
}
void tiku_trng_arch_init(void){}
int tiku_trng_arch_read_bytes(uint8_t *out,size_t length)
{
    uint32_t saved[sizeof saved_regs/sizeof saved_regs[0]],state,word=0;
    uint8_t bytes[sizeof analog_regs],changed=0;int result=TIKU_TRNG_ERR_NOT_READY;
    if((!out&&length)||length>256)return TIKU_TRNG_ERR_INVALID;
    if(!length)return TIKU_TRNG_OK;
    for(size_t n=0;n<length;n++)out[n]=0;
    state=native_irq_save();
    if(TIKU_MEM_ARCH_IN_EXCEPTION()||radio_source==TIKU_C6_TRNG_RADIO_BUSY)goto done;
    if(radio_source==TIKU_C6_TRNG_RADIO_RF){
        for(size_t n=0;n<length;n++){
            if(!(n&3u)){
                uint32_t errors=native_errors;
                uint64_t start=native_time();unsigned polls;
                for(polls=0;polls<200000&&native_time()-start<32u;polls++){}
                if(polls==200000||native_errors!=errors){result=TIKU_TRNG_ERR_TIMEOUT;goto done;}
                word=C6_REG(RNG);
            }
            out[n]=(uint8_t)word;word>>=8;
        }
        result=TIKU_TRNG_OK;goto done;
    }
    if((C6_REG(SAR)&2u)||
        (C6_REG(SAR+4)&(1u<<24))||(C6_REG(SAR+0x20)&(7u<<29))||
        (C6_REG(SAR+0x58)&(1u<<22))||C6_REG(SAR+0x40))goto done;
    for(unsigned n=0;n<sizeof saved/sizeof saved[0];n++)saved[n]=C6_REG(saved_regs[n]);
    field(0x60096080u,4u,4u);
    field(0x60096084u,0x7fffffu,1u<<22);
    field(0x600b0154u,1u<<26,0);delay(1);
    field(0x600b0154u,(1u<<26)|(1u<<27),(1u<<26)|(1u<<27));
    field(0x600af018u,4u,4u);
    for(unsigned n=0;n<sizeof bytes;n++)if(analog(analog_regs[n],&bytes[n],0))goto cleanup;
    for(unsigned n=0;n<sizeof bytes;n++){
        uint8_t value;
        if(analog_regs[n]==0||analog_regs[n]==3)value=2150u&255u;
        else if(analog_regs[n]==1||analog_regs[n]==4)value=(bytes[n]&0xf0u)|(2150u>>8);
        else value=(bytes[n]&~3u)|2u|8u|16u|64u;
        changed=(uint8_t)(n+1);if(analog(analog_regs[n],&value,1))goto cleanup;
    }
    field(SAR+0x18,0xfff000u,(37u<<18)|(37u<<12));
    field(SAR,(7u<<15)|(255u<<7)|(1u<<6)|(3u<<27)|3u,
        (1u<<15)|(15u<<7)|(1u<<6));
    field(SAR+4,(4095u<<12)|(1u<<24)|1u,(200u<<12)|(1u<<24));
    field(0x600b2800u,1u<<24,1u<<24);
    for(size_t n=0;n<length;n++){
        if(!(n&3u)){
            /* Each word follows 32 newly completed reserved-channel conversions. */
            for(unsigned fresh=0;fresh<32;fresh++){
                C6_REG(SAR+0x4c)=1u<<30;
                unsigned polls;
                for(polls=0;polls<20;polls++){
                    delay(100);if(C6_REG(SAR+0x44)&(1u<<30))break;
                }
                if(polls==20)goto cleanup;
            }
            word=C6_REG(RNG);tiku_watchdog_kick();
        }
        out[n]=(uint8_t)word;word>>=8;
    }
    result=TIKU_TRNG_OK;
cleanup:
    field(SAR+4,1u<<24,0);
    while(changed){--changed;if(analog(analog_regs[changed],&bytes[changed],1))result=TIKU_TRNG_ERR_NOT_READY;}
    for(unsigned n=sizeof saved/sizeof saved[0];n;n--)C6_REG(saved_regs[n-1])=saved[n-1];
done:
    native_irq_restore(state);
    if(result)for(size_t n=0;n<length;n++)out[n]=0;
    return result;
}
int tiku_trng_arch_read_u32(uint32_t *out)
{
    return out?tiku_trng_arch_read_bytes((uint8_t *)out,sizeof *out):TIKU_TRNG_ERR_INVALID;
}
