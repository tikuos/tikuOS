/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_native_arch.c - Native C6 process, timer, interrupt and exception qualification.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include <stddef.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_clock.h>
#include <hal/tiku_cpu.h>

#define TIMER_BIT (1u<<23)
static const uint32_t timer_registers[]={0,0x24,0x28,0x38,0x64};
static uint32_t timer_saved[5],maps_saved[C6_MAP_COUNT];
static uint8_t maps_changed[C6_MAP_COUNT];
static uint32_t plic_enable,plic_type,plic_priority,plic_threshold,cpu_mie;
static unsigned hardware_owned;
static uint64_t deadline;
static volatile uint32_t data_sentinel=0x74696b75u;
static volatile uint32_t zero_sentinel[8];
volatile uint32_t native_ticks,native_irqs,native_errors;
uint32_t *native_report;
static uint32_t event_count,callback_count,user_count;
static struct tiku_timer event_timer,callback_timer;

uint32_t native_cycles(void)
{
    uint32_t value;__asm__ volatile("csrr %0,0x7e2":"=r"(value));return value;
}
uint32_t native_irq_save(void)
{
    uint32_t value;__asm__ volatile("csrrci %0,mstatus,8":"=r"(value)::"memory");return value;
}
void native_irq_restore(uint32_t state)
{
    if(state&8u){__asm__ volatile("csrsi mstatus,8":::"memory");}
}
uint64_t native_time(void)
{
    C6_REG(C6_SYSTIMER+4)=1u<<30;
    for(unsigned n=0;n<4096;n++){
        if(C6_REG(C6_SYSTIMER+4)&(1u<<29)){
            return ((uint64_t)(C6_REG(C6_SYSTIMER+0x40)&0xfffffu)<<32)|C6_REG(C6_SYSTIMER+0x44);
        }
    }
    native_errors++;return 0;
}
static void arm_tick(uint64_t next)
{
    C6_REG(C6_SYSTIMER)&=~TIMER_BIT;
    C6_REG(C6_SYSTIMER+0x6c)=2;
    C6_REG(C6_SYSTIMER+0x24)=(uint32_t)(next>>32)&0xfffffu;
    C6_REG(C6_SYSTIMER+0x28)=(uint32_t)next;
    C6_REG(C6_SYSTIMER+0x54)=1;
    C6_REG(C6_SYSTIMER)|=TIMER_BIT;
}
int native_hardware_start(void)
{
    if((C6_REG(C6_SYSTIMER)&TIMER_BIT) || (C6_REG(C6_SYSTIMER+0x64)&2) ||
       (C6_REG(C6_SYSTIMER+0x68)&2)){return -1;}
    for(unsigned n=0;n<5;n++){timer_saved[n]=C6_REG(C6_SYSTIMER+timer_registers[n]);}
    plic_enable=C6_REG(C6_PLIC);plic_type=C6_REG(C6_PLIC+4);
    plic_priority=C6_REG(C6_PLIC+0x10+4*C6_CPU_IRQ);
    plic_threshold=C6_REG(C6_PLIC+0x90);
    __asm__ volatile("csrr %0,mie":"=r"(cpu_mie));
    for(unsigned n=0;n<C6_MAP_COUNT;n++){
        maps_saved[n]=C6_REG(C6_INTMTX+4*n);
    }
    hardware_owned=1;
    C6_REG(C6_PLIC)=0;
    __asm__ volatile("csrw mie,%0"::"r"(1u<<C6_CPU_IRQ):"memory");
    for(unsigned n=0;n<C6_MAP_COUNT;n++){
        maps_changed[n]=maps_saved[n]==C6_CPU_IRQ || n==C6_IRQ_SOURCE;
        if(maps_changed[n]){C6_REG(C6_INTMTX+4*n)=0;}
    }
    C6_REG(C6_SYSTIMER+0x38)=0;
    deadline=native_time()+C6_TICK_COUNTS;
    arm_tick(deadline);
    C6_REG(C6_SYSTIMER+0x64)|=2;
    C6_REG(C6_INTMTX+4*C6_IRQ_SOURCE)=C6_CPU_IRQ;
    C6_REG(C6_PLIC+4)=plic_type&~(1u<<C6_CPU_IRQ);
    C6_REG(C6_PLIC+0x10+4*C6_CPU_IRQ)=1;
    C6_REG(C6_PLIC+0x90)=0;
    C6_REG(C6_PLIC)=1u<<C6_CPU_IRQ;
    return 0;
}
void native_hardware_restore(void)
{
    if(!hardware_owned){return;}
    (void)native_irq_save();
    C6_REG(C6_PLIC)=0;
    C6_REG(C6_SYSTIMER)&=~TIMER_BIT;
    C6_REG(C6_SYSTIMER+0x64)&=~2u;
    C6_REG(C6_SYSTIMER+0x6c)=2;
    for(unsigned n=1;n<5;n++){C6_REG(C6_SYSTIMER+timer_registers[n])=timer_saved[n];}
    C6_REG(C6_SYSTIMER+0x54)=1;
    C6_REG(C6_SYSTIMER)=timer_saved[0];
    for(unsigned n=0;n<C6_MAP_COUNT;n++){
        if(maps_changed[n]){C6_REG(C6_INTMTX+4*n)=maps_saved[n];}
    }
    C6_REG(C6_PLIC+4)=plic_type;
    C6_REG(C6_PLIC+0x10+4*C6_CPU_IRQ)=plic_priority;
    C6_REG(C6_PLIC+0x90)=plic_threshold;
    C6_REG(C6_PLIC)=plic_enable;
    __asm__ volatile("csrw mie,%0"::"r"(cpu_mie):"memory");
    uint32_t restored_mie;__asm__ volatile("csrr %0,mie":"=r"(restored_mie));
    native_report[56]=cpu_mie;native_report[57]=restored_mie;
    unsigned mismatches=cpu_mie!=restored_mie;
    for(unsigned n=0;n<5;n++){mismatches+=C6_REG(C6_SYSTIMER+timer_registers[n])!=timer_saved[n];}
    for(unsigned n=0;n<C6_MAP_COUNT;n++){mismatches+=C6_REG(C6_INTMTX+4*n)!=maps_saved[n];}
    mismatches+=C6_REG(C6_PLIC)!=plic_enable || C6_REG(C6_PLIC+4)!=plic_type ||
                C6_REG(C6_PLIC+0x90)!=plic_threshold ||
                C6_REG(C6_PLIC+0x10+4*C6_CPU_IRQ)!=plic_priority;
    native_report[32]=mismatches;
    hardware_owned=0;
}

uint32_t *native_trap_dispatch(uint32_t *frame)
{
    uint32_t cause=frame[34];
    if(cause==((1u<<31)|C6_CPU_IRQ) && (C6_REG(C6_SYSTIMER+0x68)&2)){
        uint64_t now=native_time();
        uint32_t due=now>=deadline?(uint32_t)((now-deadline)/C6_TICK_COUNTS)+1:1;
        native_irqs++;native_ticks+=due;deadline+=(uint64_t)due*C6_TICK_COUNTS;
        arm_tick(deadline);tiku_timer_request_poll();
    }else if(cause==2 && frame[32]==(uint32_t)(uintptr_t)native_probe_illegal){
        native_report[20]++;native_report[21]=cause;native_report[22]=frame[32];native_report[23]=frame[35];
        frame[32]=(uint32_t)(uintptr_t)native_probe_after_illegal;
    }else{
        extern unsigned char native_abort[];
        NATIVE_STAGE(0x100u|(cause&255u));
        native_errors++;native_report[24]=cause;native_report[25]=frame[32];native_report[26]=frame[35];
        frame[32]=(uint32_t)(uintptr_t)native_abort;
    }
    return frame;
}

TIKU_PROCESS(native_event_process,"C6 events");
TIKU_PROCESS_THREAD(native_event_process,ev,data)
{
    (void)data;
    TIKU_PROCESS_BEGIN();
    tiku_timer_set_event(&event_timer,2);
    for(;;){
        TIKU_PROCESS_WAIT_EVENT();
        if(ev==TIKU_EVENT_TIMER){
            event_count++;tiku_timer_set_event(&event_timer,2);
        }else if(ev==TIKU_EVENT_USER){user_count++;}
    }
    TIKU_PROCESS_END();
}
static void timer_callback(void *arg)
{
    (void)arg;callback_count++;
    tiku_timer_set_callback(&callback_timer,3,timer_callback,NULL);
}

uint32_t tiku_c6_qualify(uint32_t *report)
{
    NATIVE_STAGE(3);
    native_report=report;
    for(unsigned n=0;n<C6_REPORT_WORDS;n++){report[n]=0;}
    report[0]=0x54494b55u;report[1]=1;
    report[2]=(uint32_t)(uintptr_t)__native_image_start;
    report[3]=(uint32_t)(__native_image_end-__native_image_start);
    report[4]=(uint32_t)(__bss_end-__bss_start);
    report[5]=(uint32_t)(uintptr_t)__native_stack_top;
    uint32_t good=data_sentinel==0x74696b75u;
    for(unsigned n=0;n<8;n++){good&=zero_sentinel[n]==0;zero_sentinel[n]=n+1;}
    report[6]=good;
    for(unsigned n=0;n<64;n++){native_stack_guard[n]=0xa5;}
    uint32_t *irq_guard=(uint32_t *)(void *)__native_irq_bottom;
    for(unsigned n=0;n<16;n++){irq_guard[n]=0x55aaaa55u;}
    NATIVE_STAGE(4);
    uint64_t began=native_time(),now;
    uint32_t cycle=native_cycles();
    do{now=native_time();}while(now-began<16000);
    report[7]=(uint32_t)((native_cycles()-cycle)*16u/(uint32_t)(now-began));
    NATIVE_STAGE(5);
    report[8]=((unsigned (*)(void))0x40000044u)();
    report[9]=C6_REG(0x40001000u);
    NATIVE_STAGE(6);
    tiku_process_init();tiku_timer_init();
    (void)tiku_process_register(NULL,&tiku_timer_process);
    (void)tiku_process_register(NULL,&native_event_process);
    tiku_timer_set_callback(&callback_timer,3,timer_callback,NULL);
    NATIVE_STAGE(7);
    if(native_hardware_start()) {report[33]=1;return 0;}
    NATIVE_STAGE(8);
    __asm__ volatile("csrsi mstatus,8":::"memory");
    uint32_t start=native_cycles(),limit=report[7]*180000u;
    while(native_ticks<4 && native_cycles()-start<limit){(void)tiku_process_run();}
    NATIVE_STAGE(9);
    uint32_t before=native_irqs;
    report[14]=native_irq_frame_probe(report[7]*18000u)==0;
    report[15]=native_irqs-before;
    NATIVE_STAGE(10);
    tiku_atomic_enter();
    uint32_t ticks=native_ticks;
    tiku_atomic_enter();tiku_atomic_exit();
    uint32_t state;__asm__ volatile("csrr %0,mstatus":"=r"(state));
    cycle=native_cycles();while(native_cycles()-cycle<report[7]*18000u){}
    report[16]=!(state&8u) && ticks==native_ticks;
    tiku_atomic_exit();
    while(native_ticks==ticks && native_cycles()-start<limit){}
    report[17]=native_ticks-ticks;
    NATIVE_STAGE(11);
    native_illegal_probe();
    (void)tiku_process_post(&native_event_process,TIKU_EVENT_USER,NULL);
    while(native_ticks<16 && native_cycles()-start<limit){(void)tiku_process_run();}
    NATIVE_STAGE(12);
    report[42]=C6_REG(C6_SYSTIMER+0x68);report[43]=C6_REG(C6_SYSTIMER);
    report[44]=C6_REG(C6_SYSTIMER+0x24);report[45]=C6_REG(C6_SYSTIMER+0x28);
    report[46]=C6_REG(C6_SYSTIMER+0x7c);report[47]=(uint32_t)native_time();
    report[48]=C6_REG(C6_PLIC+0x0c);
    __asm__ volatile("csrr %0,mstatus":"=r"(report[49]));
    __asm__ volatile("csrr %0,mie":"=r"(report[50]));
    report[51]=C6_REG(C6_SYSTIMER+0x38);report[52]=C6_REG(C6_INTMTX+4*C6_IRQ_SOURCE);
    report[53]=C6_REG(C6_PLIC);report[54]=C6_REG(C6_PLIC+0x90);
    report[55]=C6_REG(C6_PLIC+0x10+4*C6_CPU_IRQ);
    (void)native_irq_save();
    report[10]=native_ticks;report[11]=native_irqs;report[12]=native_errors;
    report[18]=event_count;report[19]=callback_count;report[27]=user_count;
    report[13]=1;
    for(unsigned n=0;n<64;n++){if(native_stack_guard[n]!=0xa5){report[13]=0;}}
    for(unsigned n=0;n<16;n++){if(irq_guard[n]!=0x55aaaa55u){report[13]=0;}}
    report[28]=tiku_process_count();
    report[29]=tiku_process_queue_dropped();
    report[30]=(uint32_t)(native_time()-began);
    native_hardware_restore();
    NATIVE_STAGE(13);
    report[31]=good && report[7]>=79 && report[7]<=161 && report[10]>=16 &&
               report[11]>=12 && !report[12] && report[13] && report[14] && report[15]>=2 &&
               report[16] && report[17]>=2 && report[18]>=4 && report[19]>=3 &&
               report[20]==1 && report[21]==2 && report[27]==1 && report[28]==2 &&
               !report[29] && !report[32] && report[50]==(1u<<C6_CPU_IRQ) && report[56]==report[57];
    return report[31]?0x43364b52u:0;
}
