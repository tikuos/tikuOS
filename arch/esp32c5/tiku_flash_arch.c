/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_flash_arch.c - C5 ROM flash access with SRAM staging and cache
 * exclusion. Call from SRAM; interrupts remain masked during each ROM
 * operation. SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_flash_arch.h"
#include "tiku_rom_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"

/* C5 spi1_mem_reg.h: SPI1 user transactions, independent of SPI0's cache. */
#define FLASH_COMMAND 0x60003000u
#define FLASH_USER    0x60003018u
#define FLASH_USER2   0x60003020u
#define FLASH_LENGTH  0x60003028u
#define FLASH_DATA    0x60003058u
#define FLASH_START   (1u << 18)

static uint32_t jedec;
static uint8_t ready;
static uint8_t busy;
static uint32_t stage[TIKU_FLASH_PAGE_SIZE / 4u];

/** @brief Wait for the SPI1 user transaction with a finite polling budget. */
static int command_idle(void)
{
    unsigned int tries;

    for (tries = 0; tries < 65536u; tries++) {
        if ((TIKU_C5_REG_READ(FLASH_COMMAND) & FLASH_START) == 0) {
            return 0;
        }
    }
    return -1;
}

/** @brief Read all three JEDEC bytes; the ROM status helper reads only eight
 * bits. */
static int read_identity(void)
{
    uint32_t user, user2, length, data;

    if (command_idle() != 0) {
        return -1;
    }
    user = TIKU_C5_REG_READ(FLASH_USER);
    user2 = TIKU_C5_REG_READ(FLASH_USER2);
    length = TIKU_C5_REG_READ(FLASH_LENGTH);
    data = TIKU_C5_REG_READ(FLASH_DATA);
    TIKU_C5_REG_WRITE(FLASH_USER, (1u << 31) | (1u << 28));
    TIKU_C5_REG_WRITE(FLASH_USER2, (7u << 28) | 0x9Fu);
    TIKU_C5_REG_WRITE(FLASH_LENGTH, 23u);
    TIKU_C5_REG_WRITE(FLASH_DATA, 0);
    TIKU_C5_REG_WRITE(FLASH_COMMAND, FLASH_START);
    if (command_idle() != 0) {
        return -1;
    }
    jedec = TIKU_C5_REG_READ(FLASH_DATA) & 0xFFFFFFu;
    TIKU_C5_REG_WRITE(FLASH_USER, user);
    TIKU_C5_REG_WRITE(FLASH_USER2, user2);
    TIKU_C5_REG_WRITE(FLASH_LENGTH, length);
    TIKU_C5_REG_WRITE(FLASH_DATA, data);
    return 0;
}

/** @brief Test a flash range without overflowing offset + length. */
static int valid_range(uint32_t offset, uint32_t length)
{
    return offset <= TIKU_FLASH_SIZE_BYTES &&
           length <= TIKU_FLASH_SIZE_BYTES - offset;
}

/** @brief Acquire the flash staging buffer, retaining the caller's IRQ state.
 */
static int acquire(uint32_t *state)
{
    *state = TIKU_C5_IRQ_SAVE();
    if (busy) {
        TIKU_C5_IRQ_RESTORE(*state);
        return -1;
    }
    busy = 1;
    return 0;
}

/** @brief Release staging and restore the caller's IRQ state. */
static void release(uint32_t state)
{
    busy = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

tiku_flash_err_t tiku_flash_init(void)
{
    uint32_t state, word;
    int result = TIKU_FLASH_ERR_IO;

    if (ready) {
        return TIKU_FLASH_OK;
    }
    if (acquire(&state) != 0) {
        return TIKU_FLASH_ERR_BUSY;
    }
    tiku_c5_rom_flash_attach(0, 0);
    /* The ROM's chip record keeps the manufacturer in bits 23:16 and the
     * capacity in bits 7:0, the reverse of the RDID byte order. */
    if (read_identity() != 0 || ((jedec >> 16) & 255u) != 22u ||
        tiku_c5_rom_flash_config(
            ((jedec & 255u) << 16) | (jedec & 0xFF00u) | ((jedec >> 16) & 255u),
            TIKU_FLASH_SIZE_BYTES, 65536, TIKU_FLASH_SECTOR_SIZE,
            TIKU_FLASH_PAGE_SIZE, 0xFFFF) != 0) {
        release(state);
        return TIKU_FLASH_ERR_IO;
    }
    tiku_c5_rom_cache_boot();
    tiku_c5_rom_mmu_init();
    if (tiku_c5_rom_mmu_set(0, 0, TIKU_FLASH_MMAP_BASE, 0, 64,
                            TIKU_FLASH_SIZE_BYTES / 65536u, 0) == 0) {
        tiku_c5_rom_cache_enable(0);
        if (tiku_c5_rom_cache_invalidate(TIKU_FLASH_MMAP_BASE,
                                         TIKU_FLASH_SIZE_BYTES) == 0 &&
            tiku_c5_rom_flash_read(0, &word, 4) == 0 &&
            word == *(volatile const uint32_t *)TIKU_FLASH_MMAP_BASE) {
            /* The image replaces the second-stage bootloader that clears the
             * status register's protection bits, so clear them once here;
             * the ROM helper rewrites the status register on every call. */
            uint32_t autoload = tiku_c5_rom_cache_suspend();
            int unlocked = tiku_c5_rom_flash_unlock();
            tiku_c5_rom_cache_resume(autoload);
            if (unlocked == 0) {
                ready = 1;
                result = TIKU_FLASH_OK;
            }
        }
    }
    release(state);
    return result;
}

int tiku_flash_ready(void)
{
    return ready;
}
uint32_t tiku_flash_jedec_id(void)
{
    return jedec;
}

const uint8_t *tiku_flash_map(uint32_t offset)
{
    return ready && offset < TIKU_FLASH_SIZE_BYTES
               ? (const uint8_t *)(uintptr_t)(TIKU_FLASH_MMAP_BASE + offset)
               : NULL;
}

tiku_flash_err_t tiku_flash_read(uint32_t offset, void *data, uint32_t length)
{
    uint8_t *destination = data;
    uint32_t i;

    if (!ready) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (data == NULL || !valid_range(offset, length)) {
        return TIKU_FLASH_ERR_PARAM;
    }
    for (i = 0; i < length; i++) {
        destination[i] = *(
            volatile const uint8_t *)(uintptr_t)(TIKU_FLASH_MMAP_BASE + offset +
                                                 i);
    }
    return TIKU_FLASH_OK;
}

tiku_flash_err_t tiku_flash_erase_sector(uint32_t offset)
{
    uint32_t state, autoload, i;
    int result;

    if (!ready) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (offset < TIKU_FLASH_REGION_ADDR || offset % TIKU_FLASH_SECTOR_SIZE ||
        !valid_range(offset, TIKU_FLASH_SECTOR_SIZE)) {
        return TIKU_FLASH_ERR_PARAM;
    }
    if (acquire(&state) != 0) {
        return TIKU_FLASH_ERR_BUSY;
    }
    autoload = tiku_c5_rom_cache_suspend();
    result = tiku_c5_rom_flash_erase(offset / TIKU_FLASH_SECTOR_SIZE);
    tiku_c5_rom_cache_resume(autoload);
    if (tiku_c5_rom_cache_invalidate(TIKU_FLASH_MMAP_BASE + offset,
                                     TIKU_FLASH_SECTOR_SIZE) != 0) {
        result = -1;
    }
    if (result == 0) {
        for (i = 0; i < TIKU_FLASH_SECTOR_SIZE; i++) {
            if (*(volatile const uint8_t *)(uintptr_t)(TIKU_FLASH_MMAP_BASE +
                                                       offset + i) != 255u) {
                result = -1;
                break;
            }
        }
    }
    release(state);
    return result == 0 ? TIKU_FLASH_OK : TIKU_FLASH_ERR_IO;
}

tiku_flash_err_t tiku_flash_program(uint32_t offset, const void *data,
                                    uint32_t length)
{
    const uint8_t *source = data;
    uint32_t state, autoload, i;
    int result = 0;

    if (!ready) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (data == NULL || offset < TIKU_FLASH_REGION_ADDR ||
        !valid_range(offset, length)) {
        return TIKU_FLASH_ERR_PARAM;
    }
    if (acquire(&state) != 0) {
        return TIKU_FLASH_ERR_BUSY;
    }
    while (length != 0 && result == 0) {
        uint32_t start = offset & ~3u;
        uint32_t prefix = offset - start;
        uint32_t count = TIKU_FLASH_PAGE_SIZE - offset % TIKU_FLASH_PAGE_SIZE;
        uint32_t words;
        uint8_t *bytes = (uint8_t *)stage;

        if (count > length) {
            count = length;
        }
        words = (prefix + count + 3u) & ~3u;
        for (i = 0; i < words; i++) {
            bytes[i] = *(
                volatile const uint8_t *)(uintptr_t)(TIKU_FLASH_MMAP_BASE +
                                                     start + i);
        }
        for (i = 0; i < count; i++) {
            if ((bytes[prefix + i] & source[i]) != source[i]) {
                result = -1;
                break;
            }
            bytes[prefix + i] = source[i];
        }
        if (result != 0) {
            break;
        }
        autoload = tiku_c5_rom_cache_suspend();
        result = tiku_c5_rom_flash_write(start, stage, (int32_t)words);
        tiku_c5_rom_cache_resume(autoload);
        /* Cache operations require whole 32-byte cache lines. */
        if (tiku_c5_rom_cache_invalidate(TIKU_FLASH_MMAP_BASE + (start & ~31u),
                                         ((start & 31u) + words + 31u) &
                                             ~31u) != 0) {
            result = -1;
        }
        for (i = 0; result == 0 && i < words; i++) {
            if (bytes[i] !=
                *(volatile const uint8_t *)(uintptr_t)(TIKU_FLASH_MMAP_BASE +
                                                       start + i)) {
                result = -1;
            }
        }
        offset += count;
        source += count;
        length -= count;
    }
    release(state);
    return result == 0 ? TIKU_FLASH_OK : TIKU_FLASH_ERR_IO;
}
