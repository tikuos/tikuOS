/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_owner_arch.h - FireBeetle C6 control, FIFO and loader engine.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_SDIO_OWNER_ARCH_H_
#define TIKU_P4_SDIO_OWNER_ARCH_H_
#include <hal/tiku_sdio_owner_hal.h>
/** @brief Board engine, with a private stage for unsolicited loader information. */
typedef struct {
    uint8_t scratch[512] __attribute__((aligned(64)));
    const uint8_t *tx;
    uint8_t *rx;
    uint64_t token, epoch;
    uint32_t consumed, rca, available, sent;
    uint32_t recovery[4]; /**< Request marker, ready marker, credits and offered bytes before reset. */
    uint32_t trace[5]; /**< Native epoch-reply padding sampled before peer reset. */
    tiku_clock_time_t reset_until;
    uint16_t tx_length, rx_length, offset;
    uint8_t selected, enumeration, phase, kind, resetting, booting, go, identity;
} tiku_p4_sdio_owner_t;
/** @brief Single hardware SDMMC lease across native, recovery and loader modes. */
extern const tiku_sdio_owner_ops_t tiku_p4_sdio_owner_ops;
#endif
