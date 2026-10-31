/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdcard_arch.h - SDHC/SDXC identification and scoped block writes on slot0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_SDCARD_ARCH_H_
#define TIKU_P4_SDCARD_ARCH_H_
#include <stdint.h>
#include <kernel/ipc/tiku_ipc.h>
/** @brief Card registers and the last command observation. */
typedef struct {
    uint64_t sectors;
    uint32_t cid[4], csd[4], ocr, rca, raw, response, reads, writes;
    uint32_t status, dma, transferred, elapsed_us;
    uint8_t command;
} tiku_p4_sdcard_t;
/**
 * @brief Claim an idle, opened SDMMC host and identify a 3.3 V SDHC/SDXC card.
 * @note Scheduler context, synchronous; call close after any result except BUSY.
 * No scheduler dispatch or other SDMMC calls may occur between open and close.
 */
tiku_ipc_err_t tiku_p4_sdcard_open(tiku_p4_sdcard_t *card);
/** @brief Read one 512-byte sector; rejects an LBA outside the identified capacity. */
int tiku_p4_sdcard_read(uint32_t lba, void *buffer, void *context);
/** @brief Restore host, pads and supply; a stuck engine retains its lease until reboot. */
tiku_ipc_err_t tiku_p4_sdcard_close(void);
/** @brief Authorize writes inside one explicit extent for the current lease. */
int tiku_p4_sdcard_write_enable(tiku_p4_sdcard_t *, uint64_t first, uint64_t sectors);
/** @brief Transfer one to eight sectors; writes wait for card-ready status. */
int tiku_p4_sdcard_blocks(tiku_p4_sdcard_t *, uint8_t write, uint32_t lba,
                         uint32_t count, void *data);
/** @brief Wait for programming completion and check CMD13; optional cache stays disabled. */
int tiku_p4_sdcard_sync(tiku_p4_sdcard_t *);
/** @brief Set a slot0 clock at or below 400 kHz..20 MHz while holding the lease. */
int tiku_p4_sdcard_clock(tiku_p4_sdcard_t *, uint32_t hz);
#endif
