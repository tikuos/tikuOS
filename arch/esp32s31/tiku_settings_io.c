/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_settings_io.c - SRAM-only ROM flash access for the settings reservation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_settings_internal.h"
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>
extern int esp_rom_spiflash_read(uint32_t address, uint32_t *buffer, int32_t length);
extern int esp_rom_spiflash_write(uint32_t address, const uint32_t *buffer, int32_t length);
extern int esp_rom_spiflash_erase_sector(uint32_t sector);
extern int esp_rom_spiflash_unlock(void);
extern uint32_t Cache_Suspend_L1_CORE0_ICache(void);
extern void Cache_Resume_L1_CORE0_ICache(uint32_t state);
extern uint32_t Cache_Suspend_L1_DCache(void);
extern void Cache_Resume_L1_DCache(uint32_t state);
int tiku_s31_settings_io(unsigned operation, uint32_t address, void *buffer, size_t length)
{
    uintptr_t ptr = (uintptr_t)buffer;
    if (!tiku_s31_settings_context() || operation > 2 ||
        (operation == 1 ? (address & 4095u) || length != 0 :
         ((address | ptr | length) & 3u) || !length || ptr < 0x2f000000u ||
         ptr >= 0x2f080000u || length > 0x2f080000u - ptr)) return -1;
    if (address < 0xffe000u || address >= 0x1000000u ||
        length > 0x1000000u - address) {
        if (operation || address < 0x8000u || address >= 0x9000u ||
            length > 0x9000u - address) return -1;
    }
    uint32_t irq;
    __asm__ volatile("csrrc %0,mstatus,%1" : "=r"(irq) : "r"(8u) : "memory");
    uint32_t instruction = Cache_Suspend_L1_CORE0_ICache();
    uint32_t data = Cache_Suspend_L1_DCache();
    int rc;
    if (!operation) rc = esp_rom_spiflash_read(address, buffer, (int32_t)length);
    else {
        rc = esp_rom_spiflash_unlock();
        if (!rc) rc = operation == 1 ? esp_rom_spiflash_erase_sector(address / 4096u) :
                                      esp_rom_spiflash_write(address, buffer, (int32_t)length);
    }
    Cache_Resume_L1_DCache(data);
    Cache_Resume_L1_CORE0_ICache(instruction);
    __asm__ volatile("csrw mstatus,%0" :: "r"(irq) : "memory");
    return rc ? -1 : 0;
}
int tiku_s31_settings_context(void)
{ return !tiku_s31_in_trap && tiku_thread_in_kernel(); }
