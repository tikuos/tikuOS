/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_arch.c - P4 SPI1 reads and opt-in, bounded program/erase.
 *
 * SPI1 user commands move 64-byte chunks with interrupts masked per chunk;
 * a controller or WIP timeout disables the driver until reset.
 * Registers and ECO2 ROM: ESP-IDF 4d59230ddff16327812782151ef0afef202dc6d7.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_flash_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

#define FLASH_BASE 0x5008d000u
#define FLASH_CMD (FLASH_BASE + 0x00u)
#define FLASH_ADDR (FLASH_BASE + 0x04u)
#define FLASH_CTRL (FLASH_BASE + 0x08u)
#define FLASH_CLOCK (FLASH_BASE + 0x14u)
#define FLASH_USER (FLASH_BASE + 0x18u)
#define FLASH_USER1 (FLASH_BASE + 0x1cu)
#define FLASH_USER2 (FLASH_BASE + 0x20u)
#define FLASH_LENGTH (FLASH_BASE + 0x28u)
#define FLASH_DATA (FLASH_BASE + 0x58u)
#define FLASH_ROOT 0x500e6030u
#define FLASH_START (1u << 18)
#define FLASH_ACTIVE 0xfffe0000u
#define FLASH_DIV4 0x00030103u
#define FLASH_MULTILINE 0x01906100u

#ifndef TIKU_P4_FLASH_ATTACH
#define TIKU_P4_FLASH_ATTACH() \
    ((void (*)(uint32_t, int))(uintptr_t)0x4fc001e8u)(0, 0)
#endif

static uint32_t identity;
static uint8_t ready, busy, poisoned;

/** @brief Poll a bounded number of times for outstanding SPI1 commands. */
static int idle(void)
{
    unsigned tries;
    for (tries = 0; tries < 65536u; tries++) {
        if (!(TIKU_P4_REG_READ(FLASH_CMD) & FLASH_ACTIVE)) return 1;
    }
    poisoned = 1;
    ready = 0;
    identity = 0;
    return 0;
}

/** @brief Execute one allowed read command, preserving idle SPI1 registers.
 * @note The caller masks interrupts; a stalled controller remains untouched.
 */
static int receive(unsigned command, uint32_t address, uint8_t *data,
                   unsigned length)
{
    static const uintptr_t registers[] = {
        FLASH_ADDR, FLASH_CTRL, FLASH_USER, FLASH_USER1, FLASH_USER2,
        FLASH_LENGTH
    };
    uint32_t saved[6], words[16];
    unsigned i;
    if ((command != 0x03 && command != 0x9f && command != 0x05 &&
         command != 0x35) || !length || length > 64 || !idle()) return 0;
    for (i = 0; i < 6; i++) saved[i] = TIKU_P4_REG_READ(registers[i]);
    for (i = 0; i < 16; i++) words[i] = TIKU_P4_REG_READ(FLASH_DATA + 4u * i);
    TIKU_P4_REG_WRITE(FLASH_CTRL, saved[1] & ~FLASH_MULTILINE);
    TIKU_P4_REG_WRITE(FLASH_USER, (1u << 31) | (1u << 28) |
                         (command == 0x03 ? (1u << 30) : 0));
    TIKU_P4_REG_WRITE(FLASH_USER1, command == 0x03 ? (23u << 26) : 0);
    TIKU_P4_REG_WRITE(FLASH_USER2, (7u << 28) | command);
    TIKU_P4_REG_WRITE(FLASH_ADDR, address);
    TIKU_P4_REG_WRITE(FLASH_LENGTH, length * 8u - 1u);
    TIKU_P4_REG_WRITE(FLASH_CMD, FLASH_START);
    if (!idle()) return 0;
    for (i = 0; i < length; i++) {
        data[i] = (uint8_t)(TIKU_P4_REG_READ(FLASH_DATA + (i / 4u) * 4u) >>
                            (8u * (i % 4u)));
    }
    for (i = 0; i < 6; i++) TIKU_P4_REG_WRITE(registers[i], saved[i]);
    for (i = 0; i < 16; i++) TIKU_P4_REG_WRITE(FLASH_DATA + 4u * i, words[i]);
    return 1;
}

/** @brief Claim SPI1 across a call, leaving interrupts enabled between chunks.
 */
static int acquire(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    int taken = !busy && !tiku_p4_in_isr();
    if (taken) busy = 1;
    TIKU_P4_IRQ_RESTORE(state);
    return taken;
}

/** @brief Release the call's ownership without changing the caller's MIE bit.
 */
static void release(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    busy = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

tiku_p4_flash_err_t tiku_p4_flash_init(void)
{
    uint32_t state;
    uint8_t id[3], status;
    int ok = 0;
    if (!acquire()) return TIKU_P4_FLASH_BUSY;
    if (ready) {
        release();
        return TIKU_P4_FLASH_OK;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (!poisoned && TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) == 18 &&
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) == 2 && idle()) {
        TIKU_P4_FLASH_ATTACH();
        TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK0,
                          TIKU_P4_REG_READ(TIKU_P4_SOC_CLK0) | (1u << 30));
        TIKU_P4_REG_WRITE(FLASH_ROOT,
                          (TIKU_P4_REG_READ(FLASH_ROOT) & ~0xfffu) | 12u);
        TIKU_P4_REG_WRITE(FLASH_CLOCK, FLASH_DIV4);
        if ((TIKU_P4_REG_READ(FLASH_ROOT) & 0xfffu) == 12u &&
            TIKU_P4_REG_READ(FLASH_CLOCK) == FLASH_DIV4 &&
            receive(0x9f, 0, id, sizeof(id)) &&
            id[0] == 0xef && id[1] == 0x40 && id[2] == 0x18 &&
            receive(0x05, 0, &status, 1) && !(status & 3u)) {
            identity = id[0] | (uint32_t)id[1] << 8 | (uint32_t)id[2] << 16;
            ready = 1;
            ok = 1;
        }
    }
    TIKU_P4_IRQ_RESTORE(state);
    release();
    return ok ? TIKU_P4_FLASH_OK : TIKU_P4_FLASH_IO;
}

uint32_t tiku_p4_flash_jedec(void)
{
    return identity;
}

tiku_p4_flash_err_t tiku_p4_flash_status(uint16_t *status)
{
    uint32_t state;
    uint8_t low, high;
    int ok;
    if (status == NULL) return TIKU_P4_FLASH_PARAM;
    if (!acquire()) return TIKU_P4_FLASH_BUSY;
    if (!ready) { release(); return TIKU_P4_FLASH_DOWN; }
    state = TIKU_P4_IRQ_SAVE();
    ok = receive(0x05, 0, &low, 1) && receive(0x35, 0, &high, 1);
    TIKU_P4_IRQ_RESTORE(state);
    if (ok) *status = low | (uint16_t)high << 8;
    release();
    return ok ? TIKU_P4_FLASH_OK : TIKU_P4_FLASH_IO;
}

tiku_p4_flash_err_t tiku_p4_flash_read(uint32_t offset, void *data,
                                      uint32_t length)
{
    uint8_t *destination = data;
    if (data == NULL || offset > TIKU_P4_FLASH_SIZE ||
        length > TIKU_P4_FLASH_SIZE - offset) return TIKU_P4_FLASH_PARAM;
    if (!acquire()) return TIKU_P4_FLASH_BUSY;
    if (!ready) { release(); return TIKU_P4_FLASH_DOWN; }
    while (length) {
        unsigned part = length > 64 ? 64 : length;
        uint32_t state = TIKU_P4_IRQ_SAVE();
        int ok = receive(0x03, offset, destination, part);
        TIKU_P4_IRQ_RESTORE(state);
        if (!ok) { release(); return TIKU_P4_FLASH_IO; }
        offset += part;
        destination += part;
        length -= part;
    }
    release();
    return TIKU_P4_FLASH_OK;
}

#if TIKU_P4_FLASH_WRITE_TEST || TIKU_P4_STORAGE_TEST || TIKU_P4_FIRMWARE
#include "tiku_systimer_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_mspi_arch.h"

#define FLASH_TX_LENGTH (FLASH_BASE + 0x24u)
static uint8_t write_stage[TIKU_P4_FLASH_SECTOR_SIZE];

/** @brief Check an input buffer against the two accessible internal SRAM spans.
 */
static int internal_source(uintptr_t start, uint32_t length)
{
    return (start >= TIKU_P4_SRAM_LOW_START && start < TIKU_P4_SRAM_LOW_END &&
            length <= TIKU_P4_SRAM_LOW_END - start) ||
           (start >= TIKU_P4_SRAM_HIGH_START && start < TIKU_P4_SRAM_HIGH_END &&
            length <= TIKU_P4_SRAM_HIGH_END - start);
}

#ifndef TIKU_P4_FLASH_SOURCE_VALID
#define TIKU_P4_FLASH_SOURCE_VALID(p, n) internal_source((uintptr_t)(p), (n))
#endif

/** @brief Restrict writes to the selected qualification profile's reservation.
 */
static int writable_range(uint32_t offset, uint32_t length)
{
#if TIKU_P4_STORAGE_TEST || TIKU_P4_FIRMWARE
    return offset >= TIKU_P4_FLASH_REGION_START &&
           offset <= TIKU_P4_FLASH_MIRROR_B_END &&
           length <= TIKU_P4_FLASH_MIRROR_B_END - offset;
#else
    return offset >= TIKU_P4_FLASH_SCRATCH &&
           offset <= TIKU_P4_FLASH_SCRATCH + TIKU_P4_FLASH_SECTOR_SIZE &&
           length <= TIKU_P4_FLASH_SCRATCH + TIKU_P4_FLASH_SECTOR_SIZE - offset;
#endif
}

/** @brief Send WREN, WRDI, page program or sector erase with IRQs masked.
 * @note Program commands are restricted to one page and the 64-byte FIFO.
 */
static int transmit(unsigned command, uint32_t address, const uint8_t *data,
                    unsigned length)
{
    static const uintptr_t registers[] = {
        FLASH_ADDR, FLASH_CTRL, FLASH_USER, FLASH_USER1, FLASH_USER2,
        FLASH_TX_LENGTH
    };
    uint32_t saved[6], words[16];
    unsigned i;
    if (command == 0x02) {
        if (!data || !length || length > 64 || !writable_range(address, length) ||
            length > TIKU_P4_FLASH_PAGE_SIZE - address % TIKU_P4_FLASH_PAGE_SIZE)
            return 0;
    } else if (command == 0x20) {
        if (address % TIKU_P4_FLASH_SECTOR_SIZE || length ||
            !writable_range(address, TIKU_P4_FLASH_SECTOR_SIZE)) return 0;
    } else if ((command != 0x06 && command != 0x04) || length || address) {
        return 0;
    }
    if (!idle()) return 0;
    for (i = 0; i < 6; i++) saved[i] = TIKU_P4_REG_READ(registers[i]);
    for (i = 0; i < 16; i++) words[i] = TIKU_P4_REG_READ(FLASH_DATA + 4u * i);
    TIKU_P4_REG_WRITE(FLASH_CTRL, saved[1] & ~FLASH_MULTILINE);
    TIKU_P4_REG_WRITE(FLASH_USER, (1u << 31) |
        ((command == 0x02 || command == 0x20) ? (1u << 30) : 0) |
        (length ? (1u << 27) : 0));
    TIKU_P4_REG_WRITE(FLASH_USER1,
                      (command == 0x02 || command == 0x20) ? (23u << 26) : 0);
    TIKU_P4_REG_WRITE(FLASH_USER2, (7u << 28) | command);
    TIKU_P4_REG_WRITE(FLASH_ADDR, address);
    if (length) {
        TIKU_P4_REG_WRITE(FLASH_TX_LENGTH, length * 8u - 1u);
        for (i = 0; i < length; i += 4) {
            uint32_t word = 0;
            unsigned j;
            for (j = 0; j < 4 && i + j < length; j++)
                word |= (uint32_t)data[i + j] << (8u * j);
            TIKU_P4_REG_WRITE(FLASH_DATA + i, word);
        }
    }
    TIKU_P4_REG_WRITE(FLASH_CMD, FLASH_START |
        ((command == 0x02 || command == 0x20) ? (1u << 17) : 0));
    if (!idle()) return 0;
    for (i = 0; i < 6; i++) TIKU_P4_REG_WRITE(registers[i], saved[i]);
    for (i = 0; i < 16; i++) TIKU_P4_REG_WRITE(FLASH_DATA + 4u * i, words[i]);
    return 1;
}

/** @brief Read under the existing SPI1 owner, restoring IRQs after each chunk.
 */
static int owned_read(unsigned command, uint32_t offset, uint8_t *data,
                      unsigned length)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    int ok = receive(command, offset, data, length);
    TIKU_P4_IRQ_RESTORE(state);
    return ok;
}

/** @brief Require idle, write-disabled flash with no global block protection.
 */
static int writable_status(void)
{
    uint8_t low, high;
    return owned_read(0x05, 0, &low, 1) && owned_read(0x35, 0, &high, 1) &&
           !(low & 0x1fu) && !(high & 0xc0u);
}

/** @brief Wait at most one second for WIP, with an independent poll-count cap.
 * @note The mutation guard keeps IRQs masked until the device becomes idle.
 */
static int wait_ready(void)
{
    uint64_t start, now;
    unsigned tries;
    uint8_t status;
    if (tiku_p4_systimer_read(&start)) goto failed;
    for (tries = 0; tries < 1000000u; tries++) {
        if (!owned_read(0x05, 0, &status, 1)) goto failed;
        if (!(status & 1u)) return !(status & 2u);
        if (tiku_p4_systimer_read(&now) ||
            ((now - start) & TIKU_P4_SYSTIMER_MASK) >= TIKU_P4_SYSTIMER_HZ)
            goto failed;
    }
failed:
    poisoned = 1;
    ready = 0;
    identity = 0;
    return 0;
}

/** @brief Enable one mutation, require WEL, then wait for its completion. */
static int mutate(unsigned command, uint32_t offset, const uint8_t *data,
                  unsigned length)
{
    uint8_t status;
    int ok;
    if (tiku_p4_mspi_flash_begin(offset,
        command == 0x20 ? TIKU_P4_FLASH_SECTOR_SIZE : length)) return 0;
    ok = transmit(0x06, 0, NULL, 0) && receive(0x05, 0, &status, 1) &&
         (status & 3u) == 2u && transmit(command, offset, data, length);
    if (ok) ok = wait_ready();
    if (!ok && !poisoned &&
        (!receive(0x05, 0, &status, 1) || (status & 1u))) {
        poisoned = 1;
        ready = 0;
        identity = 0;
    }
    tiku_p4_mspi_flash_end(!poisoned);
    return ok;
}

/** @brief Clear WEL on known-idle hardware; do not command a timed-out device.
 */
static int finish_write(int ok)
{
    uint32_t state;
    uint8_t status;
    int disabled;
    if (!ready) return 0;
    state = TIKU_P4_IRQ_SAVE();
    disabled = receive(0x05, 0, &status, 1) && !(status & 1u) &&
               transmit(0x04, 0, NULL, 0) && receive(0x05, 0, &status, 1) &&
               !(status & 3u);
    TIKU_P4_IRQ_RESTORE(state);
    if (!disabled) { poisoned = 1; ready = 0; identity = 0; }
    return ok && disabled;
}

tiku_p4_flash_err_t tiku_p4_flash_erase_sector(uint32_t offset)
{
    uint8_t bytes[64];
    uint32_t at;
    unsigned i;
    int ok;
    if (offset % TIKU_P4_FLASH_SECTOR_SIZE ||
        !writable_range(offset, TIKU_P4_FLASH_SECTOR_SIZE)) return TIKU_P4_FLASH_PARAM;
    if (!acquire()) return TIKU_P4_FLASH_BUSY;
    if (!ready) { release(); return TIKU_P4_FLASH_DOWN; }
    if (!writable_status()) { release(); return TIKU_P4_FLASH_IO; }
    ok = mutate(0x20, offset, NULL, 0);
    for (at = 0; ok && at < TIKU_P4_FLASH_SECTOR_SIZE; at += 64) {
        ok = owned_read(0x03, offset + at, bytes, sizeof(bytes));
        for (i = 0; ok && i < sizeof(bytes); i++) if (bytes[i] != 255) ok = 0;
    }
    ok = finish_write(ok);
    release();
    return ok ? TIKU_P4_FLASH_OK : TIKU_P4_FLASH_IO;
}

tiku_p4_flash_err_t tiku_p4_flash_program(uint32_t offset, const void *data,
                                         uint32_t length)
{
    const uint8_t *source = data;
    uint8_t bytes[64];
    uint32_t at;
    unsigned i;
    int ok = 1;
    if (!data || length > sizeof(write_stage) || !writable_range(offset, length) ||
        !TIKU_P4_FLASH_SOURCE_VALID(data, length)) return TIKU_P4_FLASH_PARAM;
    if (!acquire()) return TIKU_P4_FLASH_BUSY;
    if (!ready) { release(); return TIKU_P4_FLASH_DOWN; }
    if (!length) { release(); return TIKU_P4_FLASH_OK; }
    if (!writable_status()) { release(); return TIKU_P4_FLASH_IO; }
    for (at = 0; at < length; at++) write_stage[at] = source[at];
    for (at = 0; at < length; at += 64) {
        unsigned part = length - at > 64 ? 64 : length - at;
        if (!owned_read(0x03, offset + at, bytes, part)) {
            release(); return TIKU_P4_FLASH_IO;
        }
        for (i = 0; i < part; i++) {
            if ((bytes[i] & write_stage[at + i]) != write_stage[at + i]) {
                release(); return TIKU_P4_FLASH_NEEDS_ERASE;
            }
        }
    }
    for (at = 0; at < length && ok;) {
        unsigned part = length - at > 64 ? 64 : length - at;
        unsigned remaining = TIKU_P4_FLASH_PAGE_SIZE -
                             (offset + at) % TIKU_P4_FLASH_PAGE_SIZE;
        if (part > remaining) part = remaining;
        ok = mutate(0x02, offset + at, write_stage + at, part) &&
             owned_read(0x03, offset + at, bytes, part);
        for (i = 0; ok && i < part; i++) if (bytes[i] != write_stage[at + i]) ok = 0;
        at += part;
    }
    ok = finish_write(ok);
    release();
    return ok ? TIKU_P4_FLASH_OK : TIKU_P4_FLASH_IO;
}
#endif
