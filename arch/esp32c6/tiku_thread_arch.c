/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_thread_arch.c - C6 software-interrupt worker context backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_thread_arch.h"
#include "tiku_native_arch.h"
#include <kernel/threads/tiku_thread.h>
extern unsigned char __global_pointer$;
extern uint32_t *tiku_thread_switch(uint32_t *frame);
#ifndef C6_THREAD_MIE_ENABLE
#define C6_THREAD_MIE_ENABLE(bits) __asm__ volatile("csrs mie,%0"::"r"(bits):"memory")
#endif
void tiku_thread_arch_boot(void)
{
    uint32_t state=native_irq_save();
    C6_REG(C6_SWITCH_REQUEST)=0;
    for(unsigned n=0;n<C6_MAP_COUNT;n++){
        if(C6_REG(C6_INTMTX+4*n)==C6_SWITCH_IRQ)C6_REG(C6_INTMTX+4*n)=0;
    }
    C6_REG(C6_INTMTX+4*C6_SWITCH_SOURCE)=C6_SWITCH_IRQ;
    C6_REG(C6_PLIC+4)&=~(1u<<C6_SWITCH_IRQ);
    C6_REG(C6_PLIC+0x10+4*C6_SWITCH_IRQ)=1;
    C6_REG(C6_PLIC)|=1u<<C6_SWITCH_IRQ;
    C6_THREAD_MIE_ENABLE(1u<<C6_SWITCH_IRQ);
    native_irq_restore(state);
}
void tiku_thread_arch_pend(void)
{
    C6_REG(C6_SWITCH_REQUEST)=1;
    (void)C6_REG(C6_SWITCH_REQUEST);
}
uint32_t tiku_thread_arch_cycles(void){return native_cycles();}
uint32_t *tiku_thread_arch_frame_init(uint32_t *top,void (*entry)(void *),
                                    void *arg,void (*exit_fn)(void))
{
    uintptr_t aligned=(uintptr_t)top&~(uintptr_t)15u;
    uint32_t *frame=(uint32_t *)(aligned-C6_THREAD_FRAME_WORDS*4u);
    for(unsigned n=0;n<C6_THREAD_FRAME_WORDS;n++)frame[n]=0;
    frame[1]=(uint32_t)(uintptr_t)exit_fn;
    frame[2]=(uint32_t)aligned;
    frame[3]=(uint32_t)(uintptr_t)&__global_pointer$;
    frame[10]=(uint32_t)(uintptr_t)arg;
    frame[32]=(uint32_t)(uintptr_t)entry;
    frame[33]=(3u<<11)|(1u<<7);
    return frame;
}
uint32_t *tiku_c6_thread_dispatch(uint32_t *frame)
{
    C6_REG(C6_SWITCH_REQUEST)=0;
    (void)C6_REG(C6_SWITCH_REQUEST);
    return tiku_thread_switch(frame);
}
