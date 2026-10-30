/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_runtime_arch.c - RAM-profile CPU atomics, clock hooks and freestanding routines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include <stddef.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_timer.h>
#include <hal/tiku_cpu.h>

volatile uint8_t tiku_crit_held;
uint32_t native_saved_context;
static unsigned atomic_depth;
static uint32_t atomic_state;

void tiku_atomic_enter(void)
{
    uint32_t state=native_irq_save();
    if(!atomic_depth){atomic_state=state;}
    atomic_depth++;
}
void tiku_atomic_exit(void)
{
    if(atomic_depth && !--atomic_depth){native_irq_restore(atomic_state);}
}
/** @brief Wait for a pending interrupt with the caller's machine interrupt mask. */
static void idle_wait(void){__asm__ volatile("wfi":::"memory");}
tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode)
{return mode==TIKU_CPU_IDLE_LIGHT?idle_wait:NULL;}
#if !TIKU_SHELL_ENABLE
void tiku_vfs_unwatch_all(struct tiku_process *process)
{
    /* This profile creates no VFS nodes or watches. */
    (void)process;
}
void tiku_watchdog_kick(void)
{
    C6_REG(0x60008064u)=0x50d83aa1u;
    C6_REG(0x60008060u)=1;
    C6_REG(0x60008064u)=0;
}
#endif
void tiku_clock_arch_init(void) {}
unsigned long tiku_clock_arch_time(void) {return native_ticks;}
unsigned long tiku_clock_arch_seconds(void) {return native_ticks/128u;}
void tiku_clock_arch_wait(unsigned long ticks)
{
    uint32_t before=native_ticks;
    while(native_ticks-before<ticks){}
}
void tiku_clock_arch_delay(unsigned int us)
{
    uint64_t began=native_time();
    while(native_time()-began<(uint64_t)us*16u){}
}
unsigned char tiku_clock_arch_fault(void) {return native_errors?1:0;}
typedef uint32_t memory_word_t __attribute__((__may_alias__));
/** @brief Fill aligned words, then remaining bytes, with the low byte of value. */
void *memset(void *out,int value,size_t size)
{
    uint8_t *p=out;
    uint32_t word=(uint8_t)value*0x01010101u;
    while(size && ((uintptr_t)p&3u)){*p++=(uint8_t)value;size--;}
    while(size>=4){*(memory_word_t *)p=word;p+=4;size-=4;}
    while(size--){*p++=(uint8_t)value;}
    return out;
}
/** @brief Copy aligned words and byte tails; aligned hardware windows receive word accesses. */
void *memcpy(void *out,const void *in,size_t size)
{
    uint8_t *p=out;const uint8_t *q=in;
    if(!(((uintptr_t)p|(uintptr_t)q)&3u)){
        while(size>=4){*(memory_word_t *)p=*(const memory_word_t *)q;p+=4;q+=4;size-=4;}
    }
    while(size--){*p++=*q++;}
    return out;
}
int memcmp(const void *a,const void *b,size_t size)
{
    const uint8_t *p=a,*q=b;
    while(size--){if(*p!=*q){return *p-*q;}p++;q++;}return 0;
}
int strcmp(const char *a,const char *b)
{
    while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;
}


size_t strlen(const char *s)
{
    const char *end=s;while(*end)end++;return (size_t)(end-s);
}
void *memmove(void *out,const void *in,size_t size)
{
    uint8_t *p=out;const uint8_t *q=in;
    if((uintptr_t)p<(uintptr_t)q){for(size_t i=0;i<size;i++)p[i]=q[i];}
    else{while(size){size--;p[size]=q[size];}}return out;
}

char *strchr(const char *text,int ch)
{
    do{if((unsigned char)*text==(unsigned char)ch)return (char *)text;}while(*text++);
    return NULL;
}

#if TIKU_SHELL_ENABLE
/** @brief Refuse libc heap allocation; kernel tiers own application memory. */
void *_sbrk(int increment){(void)increment;return (void *)-1;}
#endif
