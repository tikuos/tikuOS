/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - S31 bounded memory copies on AHB DMA pair zero.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_DMA_ARCH_H_
#define TIKU_ESP32S31_DMA_ARCH_H_
#include <stdint.h>
#define TIKU_S31_DMA_OK 0
#define TIKU_S31_DMA_INVALID (-1)
#define TIKU_S31_DMA_BUSY (-2)
#define TIKU_S31_DMA_IO (-3)
#define TIKU_S31_DMA_TIMEOUT (-4)
/**
 * @brief Copy 64-byte lines synchronously using AHB DMA pair zero.
 * @note Kernel foreground CPU0; interrupts may run. Both spans must be
 * disjoint, 64-byte aligned, 64..1048576 bytes, in low 64 KiB SRAM or mapped
 * PSRAM. Caller owns both spans exclusively through return and must not
 * reset or release their allocator backing during the call. Cache handoff
 * is included. SRAM descriptors belong to this driver; pair zero is reserved.
 * @return Zero on completed copy, negative on refusal, DMA error or timeout.
 * On transfer error the destination may contain a partial copy. A failure to
 * stop the engine halts the CPU, preventing reuse of memory it may still own.
 */
int tiku_s31_dma_copy(void *destination, const void *source, uint32_t bytes);
#ifdef TIKU_S31_MEMORY_TEST
/** @brief Corrupt ownership (1) or suppress start (2) of the next test copy. */
void tiku_s31_dma_test_fault(unsigned mode);
#endif
#endif
