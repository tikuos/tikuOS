/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_flash_arch.c - ESP32-C61 external flash over the ROM's routines.
 *
 * Erase and program go through SPI1 with the cache suspended, then the bytes
 * they touched are invalidated in the window so the next read sees them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tiku_flash_arch.h"
#include "tiku_esp32c61_regs.h"

#define ROM_OK          0
#define CMD_READ_ID     0x9FU

/* Words for a source the ROM cannot read in place: it loads whole words. */
#define STAGE_WORDS     16U

static uint8_t  flash_up;
static uint32_t flash_id;
static uint32_t flash_stage[STAGE_WORDS];

/** @brief The window address of flash offset @p addr. */
static uint32_t window(uint32_t addr) {
    return TIKU_FLASH_MMAP_BASE + addr;
}

/** @brief Whether [addr, addr + len) lies inside the part. */
static int in_part(uint32_t addr, uint32_t len) {
    return addr <= TIKU_FLASH_SIZE_BYTES && len <= TIKU_FLASH_SIZE_BYTES - addr;
}

tiku_flash_err_t tiku_flash_init(void) {
    uint32_t rom_word = 0UL;

    flash_up = 0U;
    /* Single SPI on its default pins, not the legacy flash mode. */
    ESP32C61_ROM_FLASH_ATTACH(0UL, 0UL);
    if (ESP32C61_ROM_FLASH_CONFIG(0UL, TIKU_FLASH_SIZE_BYTES,
                                  TIKU_FLASH_BLOCK_SIZE, TIKU_FLASH_SECTOR_SIZE,
                                  TIKU_FLASH_PAGE_SIZE, 0xFFFFUL) != ROM_OK) {
        return TIKU_FLASH_ERR_IO;
    }
    if (ESP32C61_ROM_FLASH_UNLOCK() != ROM_OK) {
        return TIKU_FLASH_ERR_IO;
    }
    (void)ESP32C61_ROM_FLASH_USER_CMD(&flash_id, CMD_READ_ID);

    /* The whole part, one-to-one, in 64 KB pages: nothing else uses the MMU
     * while the image runs from SRAM. */
    ESP32C61_ROM_MMU_INIT();
    if (ESP32C61_ROM_MMU_SET(0UL, 0UL, TIKU_FLASH_MMAP_BASE, 0UL, 64UL,
                             TIKU_FLASH_SIZE_BYTES / 0x10000UL, 0UL) != 0) {
        return TIKU_FLASH_ERR_IO;
    }
    ESP32C61_ROM_CACHE_ENABLE(0UL);
    (void)ESP32C61_ROM_CACHE_INVAL(TIKU_FLASH_MMAP_BASE, TIKU_FLASH_SIZE_BYTES);

    /* The window and the ROM must agree, or a pointer read would lie. */
    if (ESP32C61_ROM_FLASH_READ(0UL, &rom_word, 4) != ROM_OK ||
        rom_word != *(volatile const uint32_t *)(uintptr_t)window(0UL)) {
        return TIKU_FLASH_ERR_IO;
    }
    flash_up = 1U;
    return TIKU_FLASH_OK;
}

int tiku_flash_ready(void) {
    return flash_up;
}

uint32_t tiku_flash_jedec_id(void) {
    return flash_id;
}

const uint8_t *tiku_flash_map(uint32_t addr) {
    if (!flash_up || addr >= TIKU_FLASH_SIZE_BYTES) {
        return NULL;
    }
    return (const uint8_t *)(uintptr_t)window(addr);
}

tiku_flash_err_t tiku_flash_read(uint32_t addr, void *buf, uint32_t len) {
    if (!flash_up) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (buf == NULL || !in_part(addr, len)) {
        return TIKU_FLASH_ERR_PARAM;
    }
    memcpy(buf, (const void *)(uintptr_t)window(addr), len);
    return TIKU_FLASH_OK;
}

tiku_flash_err_t tiku_flash_erase_sector(uint32_t addr) {
    uint32_t sector = addr / TIKU_FLASH_SECTOR_SIZE;
    uint32_t autoload;
    int rc;

    if (!flash_up) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (addr >= TIKU_FLASH_SIZE_BYTES) {
        return TIKU_FLASH_ERR_PARAM;
    }
    autoload = ESP32C61_ROM_CACHE_SUSPEND();
    rc = ESP32C61_ROM_FLASH_ERASE(sector);
    ESP32C61_ROM_CACHE_RESUME(autoload);
    (void)ESP32C61_ROM_CACHE_INVAL(window(sector * TIKU_FLASH_SECTOR_SIZE),
                                   TIKU_FLASH_SECTOR_SIZE);
    return rc == ROM_OK ? TIKU_FLASH_OK : TIKU_FLASH_ERR_IO;
}

/**
 * @brief Program whole words at a word-aligned address.
 *
 * A source the ROM cannot load word by word goes through the stage first.
 */
static int program_words(uint32_t addr, const uint8_t *src, uint32_t len) {
    if (((uintptr_t)src & 3U) == 0U) {
        return ESP32C61_ROM_FLASH_WRITE(addr, (const uint32_t *)(const void *)src,
                                        (int32_t)len);
    }
    while (len > 0UL) {
        uint32_t n = (len > sizeof flash_stage) ? (uint32_t)sizeof flash_stage
                                                : len;

        memcpy(flash_stage, src, n);
        if (ESP32C61_ROM_FLASH_WRITE(addr, flash_stage, (int32_t)n) != ROM_OK) {
            return -1;
        }
        addr += n;
        src  += n;
        len  -= n;
    }
    return ROM_OK;
}

tiku_flash_err_t tiku_flash_program(uint32_t addr, const void *buf, uint32_t len) {
    const uint8_t *s = (const uint8_t *)buf;
    uint32_t start = addr, total = len;
    uint32_t autoload;
    int rc = ROM_OK;

    if (!flash_up) {
        return TIKU_FLASH_ERR_DOWN;
    }
    if (buf == NULL || !in_part(addr, len)) {
        return TIKU_FLASH_ERR_PARAM;
    }
    autoload = ESP32C61_ROM_CACHE_SUSPEND();
    while (len > 0UL && rc == ROM_OK) {
        uint32_t off = addr & 3UL;

        if (off == 0UL && len >= 4UL) {
            uint32_t n = len & ~3UL;

            rc = program_words(addr, s, n);
            addr += n;
            s    += n;
            len  -= n;
        } else {
            /* A partial word: ones elsewhere leave its other bytes as they
             * are, since programming can only clear bits. */
            uint32_t n = 4UL - off;
            uint8_t w[4] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};

            if (n > len) {
                n = len;
            }
            memcpy(&w[off], s, n);
            memcpy(&flash_stage[0], w, 4U);
            rc = ESP32C61_ROM_FLASH_WRITE(addr - off, flash_stage, 4);
            addr += n;
            s    += n;
            len  -= n;
        }
    }
    ESP32C61_ROM_CACHE_RESUME(autoload);
    (void)ESP32C61_ROM_CACHE_INVAL(window(start & ~3UL),
                                   ((start & 3UL) + total + 3UL) & ~3UL);
    return rc == ROM_OK ? TIKU_FLASH_OK : TIKU_FLASH_ERR_IO;
}
