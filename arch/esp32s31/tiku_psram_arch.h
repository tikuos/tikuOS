/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.h - CPU0 octal PSRAM and cache ownership.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_PSRAM_ARCH_H_
#define TIKU_ESP32S31_PSRAM_ARCH_H_
#include <stdint.h>
#define TIKU_S31_PSRAM_BASE 0x50000000u
#define TIKU_S31_PSRAM_BYTES 0x01000000u
#define TIKU_S31_PSRAM_LINE 64u
/**
 * @brief Initialize 16 MiB AP PSRAM at 20 MHz; return zero on success.
 * @note CPU0 boot, interrupts masked, HP1 parked, no DMA or previous PSRAM
 * users. Owns PMA entry 0 and the PSRAM supply/controller. Writes the first
 * 64 bytes before publishing capacity. Repeated successful calls do nothing.
 */
int tiku_s31_psram_init(void);
/** @brief Return mapped bytes, or zero if initialization failed. */
uint32_t tiku_s31_psram_size(void);
/** @brief Return the last initialization stage, 0..5; 5 means initialization completed. */
unsigned tiku_s31_psram_stage(void);
/**
 * @brief Write back owned 64-byte lines; optionally invalidate them afterward.
 * @note Nonempty aligned span within mapped PSRAM, foreground CPU0 context.
 * The caller excludes DMA and other users of the span. Returns zero on success.
 * A timeout marks the backend unavailable; reset before further PSRAM use.
 */
int tiku_s31_psram_sync(void *address, uint32_t length, uint8_t invalidate);
/**
 * @brief Discard owned cache lines after a DMA write; return zero on success.
 * @note The span obeys sync's alignment, bounds and context rules. The caller
 * writes back and invalidates before DMA, then excludes CPU writes until this
 * operation completes. Dirty CPU data in the span is discarded.
 */
int tiku_s31_psram_invalidate(void *address, uint32_t length);
#ifdef TIKU_S31_PSRAM_TEST
/**
 * @brief Read 64 physical bytes into aligned SRAM without cache writeback.
 * @note Qualification only: no concurrent PSRAM or DMA access. Byte offset
 * must be 64-byte aligned. Returns zero on success, -1 on invalid input or timeout.
 */
int tiku_s31_psram_read_physical(uint32_t offset, uint32_t data[16]);
#endif
#endif
