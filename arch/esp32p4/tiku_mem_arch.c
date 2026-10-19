/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - P4 SRAM initialization, wiping and stack sentinels.
 *
 * Fills the 64-byte foreground and ISR stack guards with 0xa5 and checks
 * them.  Outside the storage-test and firmware builds the NVM and durable
 * calls are empty stubs; tiku_nvm_arch.c supplies them in those builds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mem_hal.h>

extern uint8_t __stack_guard[], tiku_p4_isr_guard[];

void tiku_mem_arch_init(void)
{
    unsigned i;
    for (i = 0; i < 64; i++) {
        __stack_guard[i] = 0xa5;
        tiku_p4_isr_guard[i] = 0xa5;
    }
}

int tiku_p4_memory_guards_ok(void)
{
    unsigned i;
    for (i = 0; i < 64; i++) {
        if (((volatile uint8_t *)__stack_guard)[i] != 0xa5 ||
            ((volatile uint8_t *)tiku_p4_isr_guard)[i] != 0xa5) return 0;
    }
    return 1;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len)
{
    volatile uint8_t *p = buf;
    while (len--) *p++ = 0;
}

#if !TIKU_P4_STORAGE_TEST && !TIKU_P4_FIRMWARE
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len)
{
    (void)dst; (void)src; (void)len;
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len)
{
    (void)dst; (void)src; (void)len;
}

int tiku_mem_arch_nvm_flush_status(void) { return -1; }
void tiku_mem_arch_nvm_flush(void) { }
const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    if (len) *len = 0;
    return NULL;
}
uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    if (len) *len = 0;
    return NULL;
}
#endif
