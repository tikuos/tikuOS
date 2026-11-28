/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dsi_arch.h - Revision 3 MIPI DSI host in two-lane video mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_DSI_ARCH_H_
#define TIKU_P4_DSI_ARCH_H_
#include <stdint.h>

/** @brief Power and lock the two D-PHY lanes at 900 Mbit/s, using XTAL40.
 * @return Zero on success, a negative value on regulator or link timeout.
 */
int tiku_p4_dsi_init(void);
/** @brief Send a DCS short write on virtual channel zero in command mode.
 * @param parameter A byte or -1 for a command with no payload.
 */
int tiku_p4_dsi_command(uint8_t command, int parameter);
/** @brief Read a DCS register while scanout DMA is idle. Preserve the host mode. */
int tiku_p4_dsi_read(uint8_t command, uint8_t *value);
/** @brief Start the host color bars using 1024 by 600 EK79007 video timing. */
int tiku_p4_dsi_pattern(void);
/** @brief Return one for a 64-byte aligned DMA-reachable full framebuffer. */
int tiku_p4_dsi_valid_frame(const uint16_t *pixels);
/** @brief Start continuous RGB565 scanout. CPU0 owns the display channel. */
int tiku_p4_dsi_start(const uint16_t *pixels);
/** @brief Queue a cache-clean, disjoint frame for the next DMA completion. */
int tiku_p4_dsi_queue(const uint16_t *pixels);
/** @brief Return the bridge's latched FIFO underrun indication. */
int tiku_p4_dsi_underrun(void);
/** @brief Return one after the switch, zero while waiting, negative on fault. */
int tiku_p4_dsi_showing(const uint16_t *pixels);
/** @brief Disable DPI and abort the display channel. */
void tiku_p4_dsi_stop(void);

/** @brief Consume latched underrun (bit 0) and VSYNC (bit 1) events.
 * @note Foreground observer with exclusive access. Returns cumulative DMA
 * completions and the latched channel error in the supplied outputs.
 */
uint32_t tiku_p4_dsi_events(uint32_t *transfers, int *error);
#endif
