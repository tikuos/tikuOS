/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - resident-arena SRAM admission and volatile memory operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_native_arch.h"
#include <kernel/memory/tiku_mem.h>
#include <string.h>
extern unsigned char __tier_sram_start[],__tier_sram_end[];
static tiku_mem_region_t region;
const struct tiku_mem_region *tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
    region=(tiku_mem_region_t){__native_image_start,C6_ARENA_BYTES,TIKU_MEM_REGION_SRAM};
    *count=1;return &region;
}
void tiku_mem_arch_init(void) {}
void tiku_mem_arch_secure_wipe(uint8_t *p,tiku_mem_arch_size_t n)
{ volatile uint8_t *v=p;while(n--)*v++=0; }
void tiku_mem_arch_nvm_read(uint8_t *out,const uint8_t *in,tiku_mem_arch_size_t n)
{ memcpy(out,in,n); }
void tiku_mem_arch_nvm_write(uint8_t *out,const uint8_t *in,tiku_mem_arch_size_t n)
{
    (void)out;(void)in;(void)n;
    /* The resident profile publishes no NVM region. A raw write terminates it. */
    native_errors++;((void (*)(void))0x40000090u)();
}
