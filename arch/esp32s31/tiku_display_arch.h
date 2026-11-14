/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_display_arch.h - Korvo RGB565 scanout and paired frame handoff.
 * Register fields follow ESP-IDF 4d59230 S31 LCDCAM and AXI DMA definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_DISPLAY_ARCH_H_
#define TIKU_S31_DISPLAY_ARCH_H_
#include <stdint.h>
#include <interfaces/display/tiku_display.h>
/** @brief Foreground scanout counters; poll faster than the frame period. */
typedef struct {
    uint32_t vsync, eof, underruns, dma_errors;
} tiku_s31_display_stats_t;
/** @brief Poll hardware events and copy cumulative counters; kernel context. */
int tiku_s31_display_poll(tiku_s31_display_stats_t *stats);
/** @brief Stop scanout and release storage and pins; kernel context, no ISR.
 * @note The caller retains both paired framebuffers and display structure until close.
 * Memory tiers must remain attached and unreset while the display is open.
 */
int tiku_s31_display_close(tiku_display_t *display);
#ifdef TIKU_S31_DISPLAY_TEST
/** @brief Inject descriptor-owner failure (1), stopped TX (2), or skipped switch (3). */
void tiku_s31_display_test_fault(unsigned mode);
/** @brief Stop scanout, compare the copied frame and restart; return mismatches. */
uint32_t tiku_s31_display_test_mismatches(void);
#endif
#endif
