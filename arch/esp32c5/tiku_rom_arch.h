/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_rom_arch.h - ESP32-C5 ECO 2 ROM entry points and call signatures.
 * ESP-IDF 4d59230 esp32c5.rom.ld, esp_rom_spiflash.h and C5 rom/cache.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_ROM_ARCH_H_
#define TIKU_ESP32C5_ROM_ARCH_H_
#include <stdint.h>

void tiku_c5_rom_flash_attach(uint32_t pins, int legacy);
int tiku_c5_rom_flash_config(uint32_t id, uint32_t size, uint32_t block,
                            uint32_t sector, uint32_t page, uint32_t status_mask);
int tiku_c5_rom_flash_command(uint32_t *result, uint8_t command);
int tiku_c5_rom_flash_unlock(void);
int tiku_c5_rom_flash_read(uint32_t offset, uint32_t *data, int32_t length);
int tiku_c5_rom_flash_write(uint32_t offset, const uint32_t *data, int32_t length);
int tiku_c5_rom_flash_erase(uint32_t sector);
void tiku_c5_rom_cache_boot(void);
void tiku_c5_rom_mmu_init(void);
int tiku_c5_rom_mmu_set(uint32_t encrypted, uint32_t external_ram,
                       uint32_t virtual_address, uint32_t physical_address,
                       uint32_t page_kib, uint32_t pages, uint32_t fixed);
uint32_t tiku_c5_rom_cache_suspend(void);
void tiku_c5_rom_cache_resume(uint32_t autoload);
void tiku_c5_rom_cache_enable(uint32_t autoload);
int tiku_c5_rom_cache_invalidate(uint32_t address, uint32_t length);
unsigned int tiku_c5_rom_cpu_frequency(void);
void tiku_c5_rom_cpu_frequency_set(unsigned int mhz);
void tiku_c5_rom_reset(void) __attribute__((noreturn));
uint32_t tiku_c5_rom_reset_reason(int cpu);

#endif
