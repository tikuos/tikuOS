/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdio_arch.h - C6 native SDIO target DMA and diagnostic register link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_SDIO_ARCH_H_
#define TIKU_C6_SDIO_ARCH_H_
#include <services/ipc/tiku_ipc_bus.h>
/** @brief Reset native SDIO; packet DMA stays unarmed until a committed host epoch. */
int tiku_c6_sdio_init(void);
/** @brief Return a packet engine whose fixed stages are native-arena SRAM. */
const tiku_ipc_packet_io_t *tiku_c6_sdio_io(void);
/** @brief Return the foreground nonce committed by a validated native control. */
uint64_t tiku_c6_sdio_epoch(void);
/** @brief Commit a nonzero nonce in foreground context after envelope validation. */
int tiku_c6_sdio_commit_epoch(uint64_t epoch);
/** @brief Count GPIO19 clock edges over 1 ms with idle PCNT0; kHz, or zero if unavailable. */
uint32_t tiku_c6_sdio_clock_khz(void);
/** @brief Observe engine flags, raw IRQ, receive/send descriptor control, or link state (0..5). */
uint32_t tiku_c6_sdio_observe(unsigned index);
/** @brief Preserved 12/24-byte shared-register diagnostic link. */
tiku_link_t *tiku_c6_diag_link(void);
/** @brief CRC, duplicate and delivered observations for the diagnostic link. */
uint32_t tiku_c6_diag_link_bad(void);
uint32_t tiku_c6_diag_link_duplicates(void);
uint32_t tiku_c6_diag_link_delivered(void);
/** @brief Publish five observations in unused padding of the last IPCE reply. */
void tiku_c6_diag_trace(const uint32_t values[5]);
#endif
