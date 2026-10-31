/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdmmc_arch.h - P4 slot1 SDIO commands and bounded single-descriptor DMA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_SDMMC_ARCH_H_
#define TIKU_P4_SDMMC_ARCH_H_
#include <stdint.h>
#include <kernel/ipc/tiku_ipc.h>
/** @brief Controller observations; hz is derived from the selected clock dividers. */
typedef struct {
    uint32_t hz, commands, bytes, errors, raw, dma, response, argument;
    uint8_t width, active, opened;
} tiku_p4_sdmmc_status_t;
/** @brief At exclusive board boot, stop retained DMA before zeroing device storage. */
tiku_ipc_err_t tiku_p4_sdmmc_boot_quiesce(void);
/** @brief Own slot1 at 400 kHz; board-owned C6 EN stays driven across close. */
tiku_ipc_err_t tiku_p4_sdmmc_open(void);
/** @brief Restore pads/clocks after confirmed local DMA quiescence; BUSY retains ownership. */
tiku_ipc_err_t tiku_p4_sdmmc_close(void);
/** @brief One bounded command; CMD5 uses R4 without response CRC checking. */
tiku_ipc_err_t tiku_p4_sdmmc_command(uint8_t index, uint32_t arg, uint32_t *reply);
/** @brief CMD52 byte access to function 0..7 and its 17-bit address space. */
tiku_ipc_err_t tiku_p4_sdmmc_byte(uint8_t write, uint8_t function,
    uint32_t address, uint8_t *value);
/** @brief Select the host bus width (1 or 4) after the peer's CCCR has been updated. */
tiku_ipc_err_t tiku_p4_sdmmc_width(uint8_t width);
/** @brief Select a card clock at or below requested 400 kHz..20 MHz. */
tiku_ipc_err_t tiku_p4_sdmmc_clock(uint32_t hz);
/**
 * @brief Start a byte-mode CMD53 over 1..512 bytes using one internal-SRAM stage.
 * @note The buffer begins on a 64-byte line and owns capacity whole lines,
 * 64..512 bytes. The driver retains it until poll OK or abort returns one.
 * Error results from poll require abort before reuse. No PSRAM import is accepted.
 * @note Non-word lengths above three bytes use an aligned prefix and a staged
 * one-to-three-byte tail; completion and abort cover both transactions.
 * IDMAC buffer lengths round up to four bytes within the owned capacity;
 * the card transfer length is unchanged.
 */
tiku_ipc_err_t tiku_p4_sdmmc_start(uint8_t write, uint8_t function,
    uint32_t address, uint8_t increment, void *buffer, uint16_t length, uint16_t capacity);
/** @brief Return OK after data/command completion, descriptor return and cache sync. */
tiku_ipc_err_t tiku_p4_sdmmc_poll(void);
/** @brief Stop/reset the local engine; one attests final stage access has ended. */
uint8_t tiku_p4_sdmmc_abort(void);
/** @brief Drive the wired C6 EN pad; callers provide bounded reset/boot timing. */
void tiku_p4_sdmmc_peer_enable(uint8_t enabled);
/** @brief Drive the board-reserved WAKE wire high for recovery or low for native entry. */
void tiku_p4_sdmmc_peer_recovery(uint8_t recovery);
/** @brief Copy cached observations without command execution. */
void tiku_p4_sdmmc_status(tiku_p4_sdmmc_status_t *);
/** @brief Lease the idle controller for a synchronous slot0 probe; BUSY changes nothing. */
tiku_ipc_err_t tiku_p4_sdmmc_card_claim(void);
/** @brief Release the slot0 lease after restoring registers and stopping its transfer. */
void tiku_p4_sdmmc_card_release(void);
#endif
