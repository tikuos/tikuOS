/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdcard_arch.h - CRC-checked SDHC/SDXC access over the Korvo SPI bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_SDCARD_ARCH_H_
#define TIKU_S31_SDCARD_ARCH_H_
#include <stdint.h>
/** @brief Card probing and transfer outcomes. */
enum { TIKU_S31_SD_OK = 0, TIKU_S31_SD_INVALID = -1, TIKU_S31_SD_BUSY = -2,
       TIKU_S31_SD_IO = -3, TIKU_S31_SD_TIMEOUT = -4,
       TIKU_S31_SD_NO_RESPONSE = -5, TIKU_S31_SD_UNSUPPORTED = -6 };
/** @brief Identified card and last transport observations. */
typedef struct {
    uint64_t sectors;
    uint32_t ocr, reads;
    uint8_t cid[16], csd[16], command, response;
} tiku_s31_sdcard_t;
/** @brief Power-cycle and identify a 3.3 V SDHC/SDXC card at 400 kHz.
 * @note Kernel foreground, serialized calls; reserves SPI2 and GPIO20..25/39.
 * The caller retains card until close. Failed opens release acquired resources.
 * NO_RESPONSE means CMD0 received no response, not proof of an empty slot.
 */
int tiku_s31_sdcard_open(tiku_s31_sdcard_t *card);
/** @brief Read one 512-byte sector after CRC validation; errors leave output intact.
 * @note context is the opened card. Transfer failures require close and reopen.
 */
int tiku_s31_sdcard_read(uint32_t lba, void *buffer, void *context);
/** @brief Select a write extent for this open session after checking card bounds. */
int tiku_s31_sdcard_write_enable(tiku_s31_sdcard_t *card, uint64_t first, uint64_t sectors);
/** @brief Program one authorized sector with CRC16, busy completion and CMD13 status.
 * @note Failure may leave the sector changed; close and reopen before further I/O.
 */
int tiku_s31_sdcard_write(uint32_t lba, const void *buffer, tiku_s31_sdcard_t *card);
/** @brief Wait for programming completion and check card status; no optional cache is enabled. */
int tiku_s31_sdcard_sync(tiku_s31_sdcard_t *card);
/** @brief Select 400 kHz..20 MHz after identification, rounding down to a supported SPI clock. */
int tiku_s31_sdcard_clock(tiku_s31_sdcard_t *card, uint32_t hz);
/** @brief Stop SPI, restore card-control pads and release the active card.
 * @note BUSY retains ownership when a pending write cannot be quiesced.
 */
int tiku_s31_sdcard_close(tiku_s31_sdcard_t *card);
#ifndef TIKU_S31_SD_ASYNC_ENABLE
#define TIKU_S31_SD_ASYNC_ENABLE 0
#endif
#if TIKU_S31_SD_ASYNC_ENABLE
#include "tiku_spi_arch.h"
/** @brief Caller-owned transport state retained through completion or cancellation. */
typedef struct {
    tiku_s31_spi_async_t spi;
    tiku_s31_sdcard_t *card;
    void *output;
    uint64_t timer, block_timer;
    uint32_t lba;
    uint16_t crc, offset;
    uint8_t packet[6], response[4], wire_crc[2], byte;
    uint8_t state, command_phase, response_tries, block_phase, io_pending;
} tiku_s31_sd_async_t;
/** @brief Begin power-cycled identification without waiting; return 1 when accepted.
 * @note Retain op and card unchanged until poll completes or cancel releases
 * them. Kernel foreground only. A busy refusal acquires no resources.
 */
int tiku_s31_sd_open_begin(tiku_s31_sd_async_t *, tiku_s31_sdcard_t *);
/** @brief Begin a CRC-checked sector read on the opened card; return 1 when accepted.
 * @note Retain the 512-byte output until completion or cancellation. Output is
 * unchanged on error. Other card operations are refused while this one runs.
 */
int tiku_s31_sd_read_begin(tiku_s31_sd_async_t *, tiku_s31_sdcard_t *, uint32_t lba, void *out);
/** @brief Begin one extent-authorized 512-byte write; return 1 when accepted.
 * @note Retain the input unchanged through completion or cancellation. Each poll
 * transfers at most 64 payload bytes. Completion includes busy and CMD13 checks.
 * Errors may leave the sector changed; remount before further filesystem work.
 */
int tiku_s31_sd_write_begin(tiku_s31_sd_async_t *, tiku_s31_sdcard_t *, uint32_t lba, const void *input);
/** @brief Begin programming completion and CMD13 status checks without waiting. */
int tiku_s31_sd_sync_begin(tiku_s31_sd_async_t *, tiku_s31_sdcard_t *);
/** @brief Begin a 400 kHz..20 MHz clock change on the opened card without waiting. */
int tiku_s31_sd_clock_begin(tiku_s31_sd_async_t *, tiku_s31_sdcard_t *, uint32_t hz);
/** @brief Perform one bounded transport phase; return 1 pending, 0 complete or an error.
 * @note This function does not wait for hardware or delay. Errors close the
 * session and release its pads. A failed programming operation first holds card
 * power off for 20 ms, advancing through later polls. Invalid-call refusals retain ownership.
 * Successful completion retains the
 * opened card; the caller closes it or begins another operation.
 */
int tiku_s31_sd_poll(tiku_s31_sd_async_t *);
/** @brief Cancel a read/open immediately, or advance an accepted write toward retirement.
 * @note BUSY retains operation, card and buffers; repeat cancel or poll. Once a
 * data token was sent, cancellation finishes that sector before releasing the
 * card. It does not undo a write. Failed writes reset the owned card before release.
 */
int tiku_s31_sd_cancel(tiku_s31_sd_async_t *);
#endif
#endif
