/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - C6 SRAM admission, durable writes and mapped NVM regions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include <kernel/memory/tiku_mem.h>
#include <string.h>
extern unsigned char __tier_sram_start[],__tier_sram_end[];
#ifndef TIKU_C6_NVM_REJECT
#define TIKU_C6_NVM_REJECT() ((void (*)(void))0x40000090u)()
#endif
#if TIKU_C6_STORAGE
#include "tiku_flash_arch.h"
extern unsigned char __uninit_start[],__uninit_end[];
extern unsigned char __retained_start[],__retained_end[];
static tiku_mem_region_t regions[4];
#else
static tiku_mem_region_t region;
#endif
const struct tiku_mem_region *tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
#if TIKU_C6_STORAGE
    regions[0]=(tiku_mem_region_t){__native_image_start,(uint32_t)(__uninit_start-__native_image_start),TIKU_MEM_REGION_SRAM};
    regions[1]=(tiku_mem_region_t){__uninit_start,(uint32_t)(__uninit_end-__uninit_start),TIKU_MEM_REGION_NVM};
    regions[2]=(tiku_mem_region_t){__uninit_end,C6_ARENA_BYTES-(uint32_t)(__uninit_end-__native_image_start),TIKU_MEM_REGION_SRAM};
    regions[3]=(tiku_mem_region_t){(void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+TIKU_C6_FLASH_REGION_START),TIKU_C6_FLASH_REGION_END-TIKU_C6_FLASH_REGION_START,TIKU_MEM_REGION_NVM};
    *count=4;return regions;
#else
    region=(tiku_mem_region_t){__native_image_start,C6_ARENA_BYTES,TIKU_MEM_REGION_SRAM};
    *count=1;return &region;
#endif
}
void tiku_mem_arch_init(void) {}
void tiku_mem_arch_secure_wipe(uint8_t *p,tiku_mem_arch_size_t n)
{ volatile uint8_t *v=p;while(n--)*v++=0; }
void tiku_mem_arch_nvm_read(uint8_t *out,const uint8_t *in,tiku_mem_arch_size_t n)
{ memcpy(out,in,n); }
void tiku_mem_arch_nvm_write(uint8_t *out,const uint8_t *in,tiku_mem_arch_size_t n)
{
#if TIKU_C6_STORAGE
    uintptr_t at=(uintptr_t)out,begin=(uintptr_t)__uninit_start,end=(uintptr_t)__uninit_end;
    if(at>=begin&&at<=end&&n<=end-at){memcpy(out,in,n);return;}
    begin=(uintptr_t)__retained_start;end=(uintptr_t)__retained_end;
    if(at>=begin&&at<=end&&n<=end-at){memcpy(out,in,n);return;}
#else
    (void)out;(void)in;(void)n;
#endif
    /* An invalid raw NVM destination terminates this profile. */
    native_errors++;TIKU_C6_NVM_REJECT();
}
