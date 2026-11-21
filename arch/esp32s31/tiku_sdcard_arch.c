/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sdcard_arch.c - CRC-checked SDHC/SDXC identification and sector transfers.
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
static unsigned claimed, spi_open, ready, failed, pending;
static uint64_t write_first, write_sectors;
static uint8_t staging[512];
#if TIKU_S31_SD_ASYNC_ENABLE
static tiku_s31_sd_async_t *async_operation;
#define ASYNC_BUSY (async_operation != NULL)
#define ASYNC_GUARD || ASYNC_BUSY
#else
#define ASYNC_BUSY 0
#define ASYNC_GUARD
#endif
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
/** @brief Issue one command and retain CS for its response payload. */
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
/** @brief Clock through a programming busy interval, bounded to one second. */
static int quiesce(void)
{
    uint8_t value;
    REG(0x2058300cu) = 1u << 23;
    if (byte(255, &value)) return TIKU_S31_SD_IO;
    uint64_t start = tiku_s31_time();
    for (unsigned n = 0; n < 1000000; n++) {
        if (byte(255, &value)) return TIKU_S31_SD_IO;
        if (value == 255) { pending = 0; return 0; }
        if (tiku_s31_time() - start >= 16000000u) break;
    }
    return TIKU_S31_SD_TIMEOUT;
}
int tiku_s31_sdcard_close(tiku_s31_sdcard_t *card)
{
    if (!context() || !card || card != active ASYNC_GUARD) return TIKU_S31_SD_INVALID;
    if (pending && quiesce()) return TIKU_S31_SD_BUSY;
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
    active = NULL; claimed = spi_open = ready = failed = pending = 0;
    write_first = write_sectors = 0;
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
#if TIKU_S31_SD_ASYNC_ENABLE
    if (async_operation) {
        if (tiku_s31_spi_init_begin(&async_operation->spi, &config) != 1) return TIKU_S31_SD_BUSY;
    } else
#endif
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
#if TIKU_S31_SD_ASYNC_ENABLE
    if (async_operation) return 0;
#endif
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
    if (!context() || handle != active || !active || !ready || failed ASYNC_GUARD || !buffer ||
        (uint64_t)lba >= active->sectors) return TIKU_S31_SD_INVALID;
    int result = command(17, lba);
    if (!result && active->response) result = TIKU_S31_SD_IO;
    if (!result) result = block(512);
    if (result) { failed = 1; (void)deselect(); return result; }
    memcpy(buffer, staging, 512); active->reads++;
    return 0;
}

int tiku_s31_sdcard_write_enable(tiku_s31_sdcard_t *card, uint64_t first, uint64_t sectors)
{
    if (!context() || !card || card != active || !ready || failed ASYNC_GUARD || !sectors ||
        first >= card->sectors || sectors > card->sectors - first) return TIKU_S31_SD_INVALID;
    write_first = first; write_sectors = sectors;
    return 0;
}
int tiku_s31_sdcard_sync(tiku_s31_sdcard_t *card)
{
    if (!context() || !card || card != active || !ready || failed ASYNC_GUARD) return TIKU_S31_SD_INVALID;
    int result = quiesce();
    if (!result) result = command(13, 0);
    uint8_t status = 255;
    if (!result && (card->response || byte(255, &status) || status)) result = TIKU_S31_SD_IO;
    if (deselect() && !result) result = TIKU_S31_SD_IO;
    if (result) failed = 1;
    return result;
}
int tiku_s31_sdcard_write(uint32_t lba, const void *buffer, tiku_s31_sdcard_t *card)
{
    if (!context() || !card || card != active || !ready || failed ASYNC_GUARD || !buffer ||
        (uint64_t)lba < write_first || (uint64_t)lba - write_first >= write_sectors)
        return TIKU_S31_SD_INVALID;
    int result = command(24, lba);
    if (!result && card->response) result = TIKU_S31_SD_IO;
    if (!result) {
        uint16_t sum = crc16(buffer, 512);
        uint8_t crc[2] = {sum >> 8, sum}, value;
        pending = 1;
        if (byte(255, &value) || byte(254, &value) || tiku_spi_arch_write(buffer, 512) ||
            tiku_spi_arch_write(crc, 2)) result = TIKU_S31_SD_IO;
        if (!result) {
            value = 255;
            for (unsigned n = 0; n < 16; n++) {
                if (byte(255, &value)) { result = TIKU_S31_SD_IO; break; }
                if (value != 255) break;
            }
            if (!result && (value & 31u) != 5u) result = TIKU_S31_SD_IO;
        }
        if (!result) result = tiku_s31_sdcard_sync(card);
    }
    if (result) { failed = 1; (void)deselect(); }
    return result;
}
int tiku_s31_sdcard_clock(tiku_s31_sdcard_t *card, uint32_t hz)
{
    if (!context() || !card || card != active || !ready || failed ASYNC_GUARD ||
        hz < 400000 || hz > 20000000) return TIKU_S31_SD_INVALID;
    int result = deselect();
    if (!result && tiku_s31_spi_clock(hz)) result = TIKU_S31_SD_IO;
    if (result) failed = 1;
    return result;
}

#if TIKU_S31_SD_ASYNC_ENABLE
/** @brief Compare elapsed time on the 52-bit 16 MHz system counter. */
static int elapsed(uint64_t start, uint32_t ticks)
{ return ((tiku_s31_time() - start) & ((1ULL << 52) - 1)) >= ticks; }
/** @brief Advance one FIFO operation and preserve its borrowed receive buffer. */
static int exchange(tiku_s31_sd_async_t *op, const uint8_t *tx, uint8_t *rx, unsigned n)
{
    int rc;
    if (!op->io_pending) {
        rc = tiku_s31_spi_begin(&op->spi, tx, rx, (uint16_t)n);
        if (rc == 1) op->io_pending = 1;
    } else {
        rc = tiku_s31_spi_poll(&op->spi);
        if (rc != 1) op->io_pending = 0;
    }
    return rc < 0 ? TIKU_S31_SD_IO : rc;
}
/** @brief Send one command through bounded FIFO phases and at most sixteen response bytes. */
static int command_step(tiku_s31_sd_async_t *op, unsigned cmd, uint32_t arg)
{
    int rc;
    switch (op->command_phase) {
    case 0:
        REG(0x20583008u) = 1u << 23;
        rc = exchange(op, NULL, &op->byte, 1);
        if (rc) return rc;
        op->command_phase = 1; return 1;
    case 1:
        REG(0x2058300cu) = 1u << 23;
        rc = exchange(op, NULL, &op->byte, 1);
        if (rc) return rc;
        op->command_phase = 2; return 1;
    case 2:
        op->packet[0] = (uint8_t)(64u | cmd);
        for (unsigned i = 0; i < 4; i++) op->packet[1 + i] = (uint8_t)(arg >> (24 - 8 * i));
        op->packet[5] = crc7(op->packet, 5);
        rc = exchange(op, op->packet, NULL, 6);
        if (rc) return rc;
        op->card->command = (uint8_t)cmd; op->card->response = 255;
        op->response_tries = 0; op->command_phase = 3; return 1;
    default:
        rc = exchange(op, NULL, &op->byte, 1);
        if (rc) return rc;
        if (!(op->byte & 128u)) {
            op->card->response = op->byte; op->command_phase = 0; return 0;
        }
        if (++op->response_tries == 16) return cmd ? TIKU_S31_SD_TIMEOUT : TIKU_S31_SD_NO_RESPONSE;
        return 1;
    }
}
/** @brief Extend the wire checksum over one completed FIFO batch. */
static uint16_t crc16_extend(uint16_t crc, const uint8_t *data, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0));
    }
    return crc;
}
/** @brief Receive a block in 64-byte batches, validate CRC and deselect before completion. */
static int block_step(tiku_s31_sd_async_t *op, unsigned length)
{
    int rc;
    if (!op->block_phase) {
        op->block_timer = tiku_s31_time(); op->block_phase = 1;
        op->offset = op->crc = 0;
        return 1;
    }
    if (op->block_phase == 1) {
        rc = exchange(op, NULL, &op->byte, 1);
        if (rc) return rc;
        if (op->byte == 255) return elapsed(op->block_timer, 8000000u) ? TIKU_S31_SD_TIMEOUT : 1;
        if (op->byte != 254) return TIKU_S31_SD_IO;
        op->block_phase = 2; return 1;
    }
    if (op->block_phase == 2) {
        unsigned n = length - op->offset;
        if (n > 64) n = 64;
        rc = exchange(op, NULL, staging + op->offset, n);
        if (rc) return rc;
        op->crc = crc16_extend(op->crc, staging + op->offset, n);
        op->offset += (uint16_t)n;
        if (op->offset == length) op->block_phase = 3;
        return 1;
    }
    if (op->block_phase == 3) {
        rc = exchange(op, NULL, op->wire_crc, 2);
        if (rc) return rc;
        if (op->crc != ((unsigned)op->wire_crc[0] << 8 | op->wire_crc[1])) return TIKU_S31_SD_IO;
        op->block_phase = 4; return 1;
    }
    REG(0x20583008u) = 1u << 23;
    rc = exchange(op, NULL, &op->byte, 1);
    if (rc) return rc;
    op->block_phase = 0;
    return 0;
}
/** @brief Release read-only transport ownership after aborting any active FIFO transfer. */
static int retire(tiku_s31_sd_async_t *op, int result)
{
    if (spi_open) tiku_spi_arch_close();
    spi_open = 0; async_operation = NULL;
    int rc = tiku_s31_sdcard_close(op->card);
    op->state = op->io_pending = 0;
    return rc ? rc : result;
}
int tiku_s31_sd_open_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card)
{
    if (!context() || !op || !card) return TIKU_S31_SD_INVALID;
    if (active || ASYNC_BUSY) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card = card, .state = 1};
    memset(card, 0, sizeof *card);
    active = card; async_operation = op;
    int rc = setup();
    if (rc) return retire(op, rc);
    op->timer = tiku_s31_time();
    return 1;
}
int tiku_s31_sd_verify_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card)
{
    if (!context() || !op || !card || active != card || !ready || failed) return TIKU_S31_SD_INVALID;
    if (ASYNC_BUSY || pending) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card = card, .state = 15};
    async_operation = op;
    return 1;
}
int tiku_s31_sd_read_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card, uint32_t lba, void *out)
{
    if (!context() || !op || !card || !out || active != card || !ready || failed || (uint64_t)lba >= card->sectors)
        return TIKU_S31_SD_INVALID;
    if (ASYNC_BUSY || pending) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card = card, .output = out, .lba = lba, .state = 20};
    async_operation = op;
    return 1;
}
int tiku_s31_sd_write_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card, uint32_t lba, const void *input)
{
    if (!context() || !op || !card || !input || active != card || !ready || failed ||
        (uint64_t)lba < write_first || (uint64_t)lba - write_first >= write_sectors)
        return TIKU_S31_SD_INVALID;
    if (ASYNC_BUSY || pending) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card=card,.output=(void *)input,.lba=lba,.state=30};
    async_operation = op;
    return 1;
}
int tiku_s31_sd_sync_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card)
{
    if (!context() || !op || !card || active != card || !ready || failed) return TIKU_S31_SD_INVALID;
    if (ASYNC_BUSY || pending) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card=card,.state=36,.block_timer=tiku_s31_time()};
    async_operation = op;
    REG(0x2058300cu) = 1u << 23;
    pending = 1;
    return 1;
}
int tiku_s31_sd_clock_begin(tiku_s31_sd_async_t *op, tiku_s31_sdcard_t *card, uint32_t hz)
{
    if (!context() || !op || !card || active != card || !ready || failed || hz < 400000 || hz > 20000000)
        return TIKU_S31_SD_INVALID;
    if (ASYNC_BUSY || pending) return TIKU_S31_SD_BUSY;
    *op = (tiku_s31_sd_async_t){.card = card, .lba = hz, .state = 22};
    async_operation = op;
    return 1;
}
int tiku_s31_sd_cancel(tiku_s31_sd_async_t *op)
{
    if (!context() || !op || async_operation != op) return TIKU_S31_SD_INVALID;
    if (pending || op->state == 40) {
        int rc = tiku_s31_sd_poll(op);
        if (async_operation == op) return TIKU_S31_SD_BUSY;
        if (active == op->card) return tiku_s31_sdcard_close(op->card);
        return rc < 0 ? 0 : rc;
    }
    return retire(op, 0);
}
int tiku_s31_sd_poll(tiku_s31_sd_async_t *op)
{
    if (!context() || !op || async_operation != op) return TIKU_S31_SD_INVALID;
    int rc = 0;
    switch (op->state) {
    case 1:
        rc = tiku_s31_spi_poll(&op->spi);
        if (rc < 0) rc = TIKU_S31_SD_IO;
        if (!rc) op->state = 2;
        break;
    case 2:
        if (elapsed(op->timer, 320000u)) {
            REG(0x20583018u) = 1u << 7; op->timer = tiku_s31_time(); op->state = 3;
        }
        break;
    case 3:
        if (elapsed(op->timer, 320000u)) op->state = 4;
        break;
    case 4:
        rc = exchange(op, NULL, staging, 10);
        if (!rc) op->state = 5;
        break;
    case 5: case 6: case 8: case 9: case 10: case 12: case 13: case 15: case 20: {
        unsigned cmd = op->state == 5 ? 0 : op->state == 6 ? 8 : op->state == 8 ? 55 :
            op->state == 9 ? 41 : op->state == 10 ? 58 : op->state == 12 ? 59 :
            op->state == 13 ? 9 : op->state == 15 ? 10 : 17;
        uint32_t arg = cmd == 8 ? 0x1aau : cmd == 41 ? 0x40000000u : cmd == 59 ? 1u : cmd == 17 ? op->lba : 0;
        rc = command_step(op, cmd, arg);
        if (rc) break;
        unsigned response = op->card->response;
        if (cmd == 41) {
            if (!response) op->state = 10;
            else if (response != 1) rc = TIKU_S31_SD_IO;
            else if (elapsed(op->timer, 16000000u)) rc = TIKU_S31_SD_TIMEOUT;
            else { op->block_timer = tiku_s31_time(); op->state = 18; }
        } else if ((cmd == 0 || cmd == 8) ? response != 1 : cmd == 55 ? response > 1 : response != 0)
            rc = TIKU_S31_SD_IO;
        else ++op->state;
        break;
    }
    case 7:
        rc = exchange(op, NULL, op->response, 4);
        if (!rc) {
            if (op->response[0] || op->response[1] || op->response[2] != 1 || op->response[3] != 0xaa)
                rc = TIKU_S31_SD_UNSUPPORTED;
            else { op->timer = tiku_s31_time(); op->state = 8; }
        }
        break;
    case 11:
        rc = exchange(op, NULL, op->response, 4);
        if (!rc) {
            uint8_t *r = op->response;
            op->card->ocr = (uint32_t)r[0] << 24 | (uint32_t)r[1] << 16 | (uint32_t)r[2] << 8 | r[3];
            if ((op->card->ocr & 0xc0000000u) != 0xc0000000u || !(op->card->ocr & 0x00300000u))
                rc = TIKU_S31_SD_UNSUPPORTED;
            else op->state = 12;
        }
        break;
    case 14: case 16:
        rc = block_step(op, 16);
        if (!rc) {
            if (crc7(staging, 15) != staging[15]) rc = TIKU_S31_SD_IO;
            else { memcpy(op->state == 14 ? op->card->csd : op->card->cid, staging, 16); ++op->state; }
        }
        break;
    case 17:
        if (op->card->csd[0] >> 6 != 1) { rc = TIKU_S31_SD_UNSUPPORTED; break; }
        op->card->sectors = ((uint64_t)((uint32_t)(op->card->csd[7] & 63u) << 16 |
            (uint32_t)op->card->csd[8] << 8 | op->card->csd[9]) + 1) * 1024u;
        ready = 1; op->state = 0; async_operation = NULL;
        return 0;
    case 18:
        if (elapsed(op->block_timer, 16000u)) op->state = 8;
        break;
    case 21:
        rc = block_step(op, 512);
        if (!rc) {
            memcpy(op->output, staging, 512); ++op->card->reads;
            op->state = 0; async_operation = NULL;
            return 0;
        }
        break;
    case 30:
        rc = command_step(op, 24, op->lba);
        if (!rc) {
            if (op->card->response) rc = TIKU_S31_SD_IO;
            else op->state = 31;
        }
        break;
    case 31:
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) op->state = 32;
        break;
    case 32:
        op->packet[0] = 254;
        pending = 1;
        rc = exchange(op, op->packet, NULL, 1);
        if (!rc) { op->offset = op->crc = 0; op->state = 33; }
        break;
    case 33:
        rc = exchange(op, (const uint8_t *)op->output + op->offset, NULL, 64);
        if (!rc) {
            op->crc = crc16_extend(op->crc, (const uint8_t *)op->output + op->offset, 64);
            op->offset += 64;
            if (op->offset == 512) op->state = 34;
        }
        break;
    case 34:
        op->wire_crc[0] = (uint8_t)(op->crc >> 8); op->wire_crc[1] = (uint8_t)op->crc;
        rc = exchange(op, op->wire_crc, NULL, 2);
        if (!rc) { op->response_tries = 0; op->state = 35; }
        break;
    case 35:
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) {
            if (op->byte == 255 && ++op->response_tries < 16) break;
            if ((op->byte & 31u) != 5u) rc = TIKU_S31_SD_IO;
            else { op->block_timer = tiku_s31_time(); op->state = 36; }
        }
        break;
    case 36:
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) {
            if (op->byte == 255) { pending = 0; op->state = 37; }
            else if (elapsed(op->block_timer, 16000000u)) rc = TIKU_S31_SD_TIMEOUT;
        }
        break;
    case 37:
        rc = command_step(op, 13, 0);
        if (!rc) {
            if (op->card->response) rc = TIKU_S31_SD_IO;
            else op->state = 38;
        }
        break;
    case 38:
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) {
            if (op->byte) rc = TIKU_S31_SD_IO;
            else op->state = 39;
        }
        break;
    case 39:
        REG(0x20583008u) = 1u << 23;
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) { op->state = 0; async_operation = NULL; return 0; }
        break;
    case 40:
        if (!elapsed(op->timer, 320000u)) return 1;
        pending = 0;
        return retire(op, (int32_t)op->lba);
    case 22:
        REG(0x20583008u) = 1u << 23;
        rc = exchange(op, NULL, &op->byte, 1);
        if (!rc) op->state = 23;
        break;
    case 23:
        rc = tiku_s31_spi_clock_begin(&op->spi, op->lba);
        if (rc < 0) rc = TIKU_S31_SD_IO;
        if (rc == 1) op->state = 24;
        break;
    case 24:
        rc = tiku_s31_spi_poll(&op->spi);
        if (rc < 0) rc = TIKU_S31_SD_IO;
        if (!rc) { op->state = 0; async_operation = NULL; return 0; }
        break;
    default: rc = TIKU_S31_SD_INVALID; break;
    }
    if (rc < 0 && pending) {
        if (spi_open) tiku_spi_arch_close();
        spi_open = 0;
        REG(0x20583014u) = 1u << 7;
        op->lba = (uint32_t)rc; op->timer = tiku_s31_time(); op->state = 40; op->io_pending = 0;
        return 1;
    }
    return rc < 0 ? retire(op, rc) : 1;
}
#endif
