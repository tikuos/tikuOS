/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2s_rx_arch.h - P4X 16 kHz mono PCM16 capture on I2S0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_I2S_RX_ARCH_H_
#define TIKU_P4_I2S_RX_ARCH_H_
#include <stdint.h>

/** @brief Claim the board's four I2S pads and start receive DMA, or return a negative error. */
int tiku_p4_i2s_rx_start(void);
/** @brief Read up to n samples without waiting; a negative result indicates a capture fault. */
int tiku_p4_i2s_rx_read(int16_t *out, unsigned n);
/** @brief Stop DMA and release the ring and pads. */
void tiku_p4_i2s_rx_stop(void);
#endif
