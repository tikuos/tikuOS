/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.h - Internal SRAM geometry and software stack guards.
 *
 * Low SRAM ends at the ROM workspace and high SRAM at the 128 KiB L2
 * cache; the SRAM tier takes its main and spare spans from the linker
 * script.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_MEM_ARCH_H_
#define TIKU_ESP32P4_MEM_ARCH_H_
#include <stdint.h>
#define TIKU_MEM_ARCH_ALIGNMENT 4u
#define TIKU_MEM_ARCH_SIZE_T_DEFINED
typedef uint32_t tiku_mem_arch_size_t;
#define TIKU_TIER_SRAM_DERIVED 1
#define TIKU_TIER_SRAM_EXTRA 1
/** @brief Low internal SRAM below the excluded ROM workspace. */
#define TIKU_P4_SRAM_LOW_START UINT32_C(0x4ff00000)
#define TIKU_P4_SRAM_LOW_END UINT32_C(0x4ff2bbd0)
/** @brief High internal SRAM below the fixed 128 KiB cache allocation. */
#define TIKU_P4_SRAM_HIGH_START UINT32_C(0x4ff40000)
#define TIKU_P4_SRAM_HIGH_END UINT32_C(0x4ffa0000)
/** @brief Non-zero if the foreground and ISR stack guard bytes are intact. */
int tiku_p4_memory_guards_ok(void);
#endif
