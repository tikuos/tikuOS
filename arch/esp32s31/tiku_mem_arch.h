/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - S31 internal allocation and external-memory attachment.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_MEM_ARCH_H_
#define TIKU_ESP32S31_MEM_ARCH_H_
#include <stdint.h>
#define TIKU_MEM_ARCH_ALIGNMENT 4u
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#define TIKU_TIER_SRAM_DERIVED 1
/**
 * @brief Initialize SRAM and attach qualified PSRAM; return zero on success.
 * @note CPU0 boot, interrupts masked, no workers or DMA users. Repeated calls
 * preserve reservations. The SRAM tier occupies the linker gap below the stack.
 */
int tiku_s31_memory_init(void);
#endif
