/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mspi_arch.h - CPU0 cache exclusion for native flash mutations.
 *
 * Each SPI1 program or erase runs between tiku_p4_mspi_flash_begin() and
 * tiku_p4_mspi_flash_end(), from internal SRAM, while the cache cannot
 * reach flash or PSRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_MSPI_ARCH_H_
#define TIKU_P4_MSPI_ARCH_H_
#include <stdint.h>
/** @brief Mask interrupts and suspend external cache access; zero means success.
 * @note The caller, its stack, constants and buffers must be internal SRAM.
 * CPU1/LP must be parked, with DMA stopped. Pair success with flash_end().
 * ISR, nested and invalid-range requests fail without changing cache state.
 */
int tiku_p4_mspi_flash_begin(uint32_t offset, uint32_t length);
/** @brief Invalidate the changed flash range and restore cache and CPU state.
 * @note readable must be nonzero only when flash and SPI1 are known idle.
 * Uncertain flash state or failed cache maintenance traps with IRQs masked;
 * execution cannot return to possibly unreadable or stale flash instructions.
 */
void tiku_p4_mspi_flash_end(int readable);
#endif
