/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_psram_arch.h - C5 native-CS1 quad PSRAM, CPU-only cached allocation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_PSRAM_ARCH_H_
#define TIKU_ESP32C5_PSRAM_ARCH_H_
#include <stdint.h>

#define TIKU_C5_PSRAM_BASE 0x42800000u
#ifndef TIKU_C5_PSRAM_EXTERNAL
#define TIKU_C5_PSRAM_EXTERNAL 0
#endif
typedef enum {
    TIKU_C5_PSRAM_OK = 0,
    TIKU_C5_PSRAM_ABSENT = -1,
    TIKU_C5_PSRAM_ID = -2,
    TIKU_C5_PSRAM_IO = -3,
    TIKU_C5_PSRAM_MAP = -4,
    TIKU_C5_PSRAM_VERIFY = -5,
    TIKU_C5_PSRAM_BUSY = -6,
    TIKU_C5_PSRAM_CONFIG = -7
} tiku_c5_psram_err_t;

/** @brief Identify, map and verify PSRAM; kernel foreground only, after flash init.
 * @note GPIO15/18/19 use native MSPI routing. No PSRAM encryption or DMA support.
 *       A zero package code returns ABSENT unless TIKU_C5_PSRAM_EXTERNAL=1.
 */
int tiku_c5_psram_init(void);
/** @brief Initialize and attach verified capacity to the external memory tier. */
int tiku_c5_psram_attach(void);
/** @brief Detach without forcing live allocations, then unmap and restore controller state. */
int tiku_c5_psram_down(void);
/** @brief Verified mapped bytes, zero before successful initialization. */
uint32_t tiku_c5_psram_size(void);
/** @brief Last read 24-bit device ID, zero before probing. */
uint32_t tiku_c5_psram_id(void);
/** @brief Last lifecycle result; zero before the first operation. */
int tiku_c5_psram_result(void);
/** @brief Whether the driver has attached its mapping to the allocator. */
int tiku_c5_psram_attached(void);
/** @brief Clean and invalidate complete cache lines covering a mapped range. */
int tiku_c5_psram_sync(const void *address, uint32_t length);
#endif
