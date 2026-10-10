/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mem_arch.h - C5 durable SRAM and alternating flash mirror slots.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_MEM_ARCH_H_
#define TIKU_ESP32C5_MEM_ARCH_H_
#include <stdint.h>
#include <stddef.h>

#define TIKU_MEM_ARCH_ALIGNMENT 4u
#ifndef TIKU_MEM_ARCH_SIZE_T_DEFINED
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#endif

/** @brief Zero the durable working copy, then restore the newest valid mirror.
 */
void tiku_mem_arch_init(void);
/** @brief Zero bytes through volatile stores; NULL is ignored. */
void tiku_mem_arch_secure_wipe(uint8_t *data, tiku_mem_arch_size_t length);
/** @brief Copy bytes from NVM; NULL arguments are ignored. */
void tiku_mem_arch_nvm_read(uint8_t *destination, const uint8_t *source,
                            tiku_mem_arch_size_t length);
/** @brief Copy into the durable SRAM span; an out-of-span write is ignored. */
void tiku_mem_arch_nvm_write(uint8_t *destination, const uint8_t *source,
                             tiku_mem_arch_size_t length);
/** @brief Commit the working copy; return -1 on flash, integrity or size
 * failure. */
int tiku_mem_arch_nvm_flush_status(void);
/** @brief Commit the working copy and discard its status. */
void tiku_mem_arch_nvm_flush(void);
/** @brief Return a tiku_nvm_restore_t code describing the boot restore. */
int tiku_mem_arch_nvm_restore_status(void);
/** @brief Number of mirror updates successfully completed since initialization.
 */
uint32_t tiku_mem_arch_nvm_program_count(void);
/** @brief Return the latest CRC-validated durable image and its byte count. */
const uint8_t *tiku_mem_arch_durable(size_t *length);
/** @brief Return the SRAM working copy and its byte count, excluding retained
 * data. */
uint8_t *tiku_mem_arch_durable_live(size_t *length);
/** @brief Return the selected mirror header, or NULL when no slot validates. */
const uint8_t *tiku_mem_arch_nvm_mirror(void);
#endif
