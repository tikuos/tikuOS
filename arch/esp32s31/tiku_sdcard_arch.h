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
#endif
