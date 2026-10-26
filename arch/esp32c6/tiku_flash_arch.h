/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_arch.h - Native C6 flash mapping and reserved-tail writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_FLASH_ARCH_H
#define TIKU_C6_FLASH_ARCH_H
#include <stdint.h>
#define TIKU_C6_FLASH_SIZE 0x400000u
#define TIKU_C6_FLASH_VIRTUAL 0x42000000u
#define TIKU_C6_FLASH_SECTOR_SIZE 4096u
#define TIKU_C6_FLASH_REGION_START 0x331000u
#define TIKU_C6_FLASH_REGION_END 0x3fc000u
#define TIKU_C6_FLASH_MIRROR_A_START 0x3fc000u
#define TIKU_C6_FLASH_MIRROR_A_END 0x3fe000u
#define TIKU_C6_FLASH_MIRROR_B_START 0x3fe000u
#define TIKU_C6_FLASH_MIRROR_B_END 0x400000u
/** @brief Map the 4 MiB flash and validate the ROM geometry and resident owner. */
int tiku_c6_flash_init(void);
/** @brief Return the initialized JEDEC identity, or zero after an I/O failure. */
uint32_t tiku_c6_flash_jedec(void);
/** @brief Copy mapped flash bytes into SRAM; return zero on success. */
int tiku_c6_flash_read(uint32_t offset,void *data,uint32_t length);
/** @brief Program bytes in the reserved tail; refuse 0-to-1 transitions before writing.
 * @note Input must be stable SRAM; cache and IRQs are suspended per page.
 */
int tiku_c6_flash_program(uint32_t offset,const void *data,uint32_t length);
/** @brief Erase one aligned tail sector; return zero after readback verification. */
int tiku_c6_flash_erase_sector(uint32_t offset);
#if TIKU_C6_STORAGE_TEST
/** @brief Return the latest SPI1 phase and status in a qualification image. */
uint32_t tiku_c6_flash_trace(void);
#endif
/** @brief Replay an interrupted region rewrite before exposing NVM consumers. */
int tiku_c6_region_recover(void);
/** @brief Finish blank-region provisioning; subsequent writes use recovery records. */
void tiku_c6_region_seal(void);
#endif
