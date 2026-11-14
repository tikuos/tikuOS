/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdcard_arch.c - CRC-checked SDHC/SDXC identification and sector reads.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <string.h>
#include <hal/tiku_clock_hal.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <kernel/threads/tiku_thread.h>
#include "tiku_sdcard_arch.h"
#include "tiku_spi_arch.h"
#include "tiku_runtime_arch.h"
#ifndef TIKU_S31_SD_REG
#define TIKU_S31_SD_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define REG(a) TIKU_S31_SD_REG(a)
#define MUX(p) (0x20582000u + 4u * (p))
#define CONFIG(p) (0x205830f4u + 4u * (p))
#define ROUTE(p) (0x20583af4u + 4u * (p))
static const unsigned pins[] = {21,22,23,39};
static const char name[] = "sdcard";
static tiku_s31_sdcard_t *active;
static uint32_t saved[4][5];
static unsigned claimed, spi_open, ready, failed;
static uint8_t staging[512];
/** @brief Restrict card and controller operations to kernel foreground. */
static int context(void) { return !tiku_s31_in_trap && tiku_thread_in_kernel(); }
/** @brief SD CRC7 with the command end bit, polynomial x^7+x^3+1. */
static uint8_t crc7(const uint8_t *data, unsigned length)
{
    uint8_t crc = 0;
    while (length--) {
        uint8_t value = *data++;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc <<= 1;
            if ((value ^ crc) & 128u) crc ^= 9u;
            value <<= 1;
        }
    }
    return (uint8_t)((crc << 1) | 1u);
}
/** @brief SD data CRC16, polynomial x^16+x^12+x^5+1, initial zero. */
static uint16_t crc16(const uint8_t *data, unsigned length)
{
    uint16_t crc = 0;
    while (length--) {
        crc ^= (uint16_t)*data++ << 8;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0));
    }
    return crc;
}
/** @brief Exchange a byte while retaining transport errors. */
static int byte(uint8_t tx, uint8_t *rx)
{ return tiku_spi_arch_write_read(&tx, rx, 1) ? TIKU_S31_SD_IO : 0; }
/** @brief Release CS and send eight clocks before the next command. */
static int deselect(void)
{ uint8_t ignored; REG(0x20583008u) = 1u << 23; return byte(255, &ignored); }
/** @brief Issue one read/control command and retain CS for its response payload. */
static int command(unsigned index, uint32_t argument)
{
    uint8_t packet[6] = {64u | index, argument >> 24, argument >> 16, argument >> 8, argument, 0};
    uint8_t r = 255;
    active->command = index; active->response = 255;
    if (deselect()) return TIKU_S31_SD_IO;
    REG(0x2058300cu) = 1u << 23;
    if (byte(255, &r)) return TIKU_S31_SD_IO;
    packet[5] = crc7(packet, 5);
    if (tiku_spi_arch_write(packet, 6)) return TIKU_S31_SD_IO;
    for (unsigned n = 0; n < 16; n++) {
        if (byte(255, &r)) return TIKU_S31_SD_IO;
        if (!(r & 128u)) { active->response = r; return 0; }
    }
    return index == 0 ? TIKU_S31_SD_NO_RESPONSE : TIKU_S31_SD_TIMEOUT;
}
/** @brief Receive a 16- or 512-byte block and check its wire CRC before use. */
static int block(unsigned length)
{
    uint8_t token = 255, crc[2];
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 32768; n++) {
        if (byte(255, &token)) return TIKU_S31_SD_IO;
        if (token != 255) break;
        if (tiku_s31_time() - start >= 8000000u) return TIKU_S31_SD_TIMEOUT;
    }
    if (token == 255) return TIKU_S31_SD_TIMEOUT;
    if (token != 254) return TIKU_S31_SD_IO;
    if (tiku_spi_arch_read(staging, length) || tiku_spi_arch_read(crc, 2)) return TIKU_S31_SD_IO;
    if (crc16(staging, length) != ((unsigned)crc[0] << 8 | crc[1])) return TIKU_S31_SD_IO;
    return deselect();
}
int tiku_s31_sdcard_close(tiku_s31_sdcard_t *card)
{
    if (!context() || !card || card != active) return TIKU_S31_SD_INVALID;
    if (spi_open) {
        REG(0x20583008u) = 1u << 23;
        tiku_spi_arch_close();
    }
    for (unsigned i = 0; i < claimed; i++) {
        unsigned p = pins[i], bit = 1u << (p & 31u);
        REG(p < 32 ? 0x2058303cu : 0x20583048u) = bit;
        REG(p < 32 ? (saved[i][4] ? 0x20583008u : 0x2058300cu) :
                     (saved[i][4] ? 0x20583014u : 0x20583018u)) = bit;
        REG(ROUTE(p)) = saved[i][1]; REG(CONFIG(p)) = saved[i][2]; REG(MUX(p)) = saved[i][0];
        if (saved[i][3]) REG(p < 32 ? 0x20583038u : 0x20583044u) = bit;
        (void)tiku_gpio_release(p / 8 + 1, p % 8, name);
    }
    active = NULL; claimed = spi_open = ready = failed = 0;
    return 0;
}
/** @brief Claim card-control pads and establish SPI mode with a power cycle. */
static int setup(void)
{
    for (unsigned i = 0; i < 4; i++) {
        unsigned p = pins[i], bit = 1u << (p & 31u);
        if (tiku_gpio_claim(p / 8 + 1, p % 8, name)) return TIKU_S31_SD_BUSY;
        saved[i][0] = REG(MUX(p)); saved[i][1] = REG(ROUTE(p)); saved[i][2] = REG(CONFIG(p));
        saved[i][3] = REG(p < 32 ? 0x20583034u : 0x20583040u) & bit;
        saved[i][4] = REG(p < 32 ? 0x20583004u : 0x20583010u) & bit;
        claimed++;
    }
    const tiku_spi_config_t config = {0,0,100};
    if (tiku_spi_arch_init(&config)) return TIKU_S31_SD_BUSY;
    spi_open = 1;
    for (unsigned i = 0; i < 4; i++) {
        unsigned p = pins[i], bit = 1u << (p & 31u);
        REG(p < 32 ? 0x2058303cu : 0x20583048u) = bit;
        REG(MUX(p)) = (saved[i][0] & ~((7u << 12) | (3u << 7))) |
                      (1u << 12) | (1u << 9) | (1u << 8);
        REG(CONFIG(p)) = saved[i][2] & ~4u;
        REG(ROUTE(p)) = 256u | (1u << 10);
    }
    REG(0x20583008u) = 1u << 23; REG(0x20583038u) = 1u << 23;
    REG(0x20583014u) = 1u << 7; REG(0x20583044u) = 1u << 7;
    tiku_clock_arch_delay(20000);
    REG(0x20583018u) = 1u << 7;
    tiku_clock_arch_delay(20000);
    uint8_t idle[10];
    return tiku_spi_arch_read(idle, sizeof idle) ? TIKU_S31_SD_IO : 0;
}
int tiku_s31_sdcard_open(tiku_s31_sdcard_t *card)
{
    if (!context() || !card) return TIKU_S31_SD_INVALID;
    if (active) return TIKU_S31_SD_BUSY;
    memset(card, 0, sizeof *card); active = card;
    int result = setup();
    if (result) goto fail;
    result = command(0, 0); if (result) goto fail;
    if (card->response != 1) { result = TIKU_S31_SD_IO; goto fail; }
    result = command(8, 0x1aa); if (result) goto fail;
    if (card->response != 1) { result = TIKU_S31_SD_UNSUPPORTED; goto fail; }
    uint8_t r[4];
    if (tiku_spi_arch_read(r, 4) || r[0] || r[1] || r[2] != 1 || r[3] != 0xaa) {
        result = TIKU_S31_SD_IO; goto fail;
    }
    uint64_t start = tiku_s31_time();
    for (unsigned i = 0; i < 1000; i++) {
        result = command(55, 0); if (result) goto fail;
        if (card->response > 1) { result = TIKU_S31_SD_IO; goto fail; }
        result = command(41, 0x40000000u); if (result) goto fail;
        if (card->response == 0) break;
        if (card->response != 1) { result = TIKU_S31_SD_IO; goto fail; }
        if (tiku_s31_time() - start >= 16000000u || i == 999) { result = TIKU_S31_SD_TIMEOUT; goto fail; }
        tiku_clock_arch_delay(1000);
    }
    result = command(58, 0); if (result) goto fail;
    if (card->response || tiku_spi_arch_read(r, 4)) { result = TIKU_S31_SD_IO; goto fail; }
    card->ocr = (uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3];
    if ((card->ocr & 0xc0000000u) != 0xc0000000u || !(card->ocr & 0x00300000u)) {
        result = TIKU_S31_SD_UNSUPPORTED; goto fail;
    }
    result = command(59, 1); if (result) goto fail;
    if (card->response) { result = TIKU_S31_SD_UNSUPPORTED; goto fail; }
    for (unsigned cmd = 9; cmd <= 10; cmd++) {
        result = command(cmd, 0); if (result) goto fail;
        if (card->response) { result = TIKU_S31_SD_IO; goto fail; }
        result = block(16); if (result) goto fail;
        if (crc7(staging, 15) != staging[15]) { result = TIKU_S31_SD_IO; goto fail; }
        memcpy(cmd == 9 ? card->csd : card->cid, staging, 16);
    }
    if (card->csd[0] >> 6 != 1) { result = TIKU_S31_SD_UNSUPPORTED; goto fail; }
    uint32_t size = (uint32_t)(card->csd[7] & 63u) << 16 | (uint32_t)card->csd[8] << 8 | card->csd[9];
    card->sectors = ((uint64_t)size + 1) * 1024u;
    ready = 1;
    return 0;
fail:
    (void)tiku_s31_sdcard_close(card);
    return result;
}
int tiku_s31_sdcard_read(uint32_t lba, void *buffer, void *handle)
{
    if (!context() || handle != active || !active || !ready || failed || !buffer ||
        (uint64_t)lba >= active->sectors) return TIKU_S31_SD_INVALID;
    int result = command(17, lba);
    if (!result && active->response) result = TIKU_S31_SD_IO;
    if (!result) result = block(512);
    if (result) { failed = 1; (void)deselect(); return result; }
    memcpy(buffer, staging, 512); active->reads++;
    return 0;
}
