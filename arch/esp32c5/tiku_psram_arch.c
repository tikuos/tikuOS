/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_psram_arch.c - C5 quad PSRAM identification, mapping and tier lifecycle.
 * Register fields follow ESP-IDF 4d59230 C5 spi_mem_c_reg.h and spi1_mem_reg.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_psram_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_rom_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_timer_arch.h"
#include "tiku_esp32c5_regs.h"
#include <kernel/memory/tiku_mem.h>
#if (TIKU_THREADS_ENABLE + 0)
#include <kernel/threads/tiku_thread.h>
#endif

#define SPI0     0x60002000u
#define SPI1     0x60003000u
#define MUX(pin) (0x60090000u + 4u * (pin))
#define START    (1u << 18)
#ifndef TIKU_C5_PSRAM_PMA_READ
#define TIKU_C5_PSRAM_PMA_READ(cfg, addr)                                      \
    __asm__ volatile("csrr %0, 0xbcd\ncsrr %1, 0xbdd" : "=r"(cfg), "=r"(addr))
#define TIKU_C5_PSRAM_PMA_WRITE(cfg, addr)                                     \
    __asm__ volatile(                                                          \
        "csrw 0xbdd, %1\ncsrw 0xbcd, %0\nfence rw,rw" ::"r"((uint32_t)(cfg)),  \
        "r"((uint32_t)(addr))                                                  \
        : "memory")
#endif
#ifndef TIKU_C5_PSRAM_WORD
#define TIKU_C5_PSRAM_WORD(offset)                                             \
    (*(volatile uint32_t *)(uintptr_t)(TIKU_C5_PSRAM_BASE + (offset)))
#endif

static uint32_t capacity, device_id, mapped_pages;
static int last_result;
static uint8_t attached, changing;
static const uint16_t spi1_offsets[] = {4,    8,    0x14, 0x18, 0x1c, 0x20,
                                        0x24, 0x28, 0x34, 0x58, 0x5c};
static const uint16_t spi0_offsets[] = {0x40, 0x48, 0x4c, 0x50, 0x174, 0x1a0};
/* MSPI pads the PSRAM needs on top of the flash: CS1, WP and HD. */
static const uint8_t pads[3] = {15, 18, 20};
static uint32_t saved_spi0[6], saved_mux[3], saved_pma, saved_address;

/* Cache sync unit (ESP-IDF 4d59230 C5 cache_reg.h). */
#define CACHE_SYNC_CTRL                 0x600C8094u
#define CACHE_SYNC_MAP                  0x600C8098u
#define CACHE_SYNC_ADDR                 0x600C809Cu
#define CACHE_SYNC_SIZE                 0x600C80A0u
#define CACHE_SYNC_WRITEBACK_INVALIDATE (1u << 3)
#define CACHE_SYNC_DONE                 (1u << 4)

/**
 * @brief Write back and invalidate a 32-byte-aligned span of the cache.
 *
 * The C5 ROM's Cache_WriteBack_Invalidate_Addr issues the sync once, which
 * can lose lines; this issues it twice, as ESP-IDF's
 * esp_rom_cache_writeback_esp32c5_esp32c61_esp32h4.c patch does.
 */
static int cache_writeback_invalidate(uint32_t address, uint32_t length)
{
    unsigned pass;

    TIKU_C5_REG_WRITE(CACHE_SYNC_MAP, 1u << 4);
    TIKU_C5_REG_WRITE(CACHE_SYNC_ADDR, address);
    TIKU_C5_REG_WRITE(CACHE_SYNC_SIZE, length);
    for (pass = 0; pass < 2; pass++) {
        TIKU_C5_REG_WRITE(CACHE_SYNC_CTRL, CACHE_SYNC_WRITEBACK_INVALIDATE);
        while (!(TIKU_C5_REG_READ(CACHE_SYNC_CTRL) & CACHE_SYNC_DONE)) {
        }
    }
    return 0;
}

/** @brief Serialize kernel foreground lifecycle calls with interrupts held off.
 */
static int enter(uint32_t *state)
{
    *state = TIKU_C5_IRQ_SAVE();
    if (!(*state & 8u) || changing
#if (TIKU_THREADS_ENABLE + 0)
        || !tiku_thread_in_kernel()
#endif
    ) {
        TIKU_C5_IRQ_RESTORE(*state);
        return TIKU_C5_PSRAM_BUSY;
    }
    changing = 1;
    return 0;
}

/** @brief Finish a lifecycle call and restore the caller's interrupt state. */
static int leave(uint32_t state, int result)
{
    last_result = result;
    changing = 0;
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

/** @brief Wait a finite number of polls for SPI1 to finish a user command. */
static int idle(void)
{
    unsigned i;
    for (i = 0; i < 65536u; i++) {
        if (!(TIKU_C5_REG_READ(SPI1) & START)) {
            return 0;
        }
    }
    return -1;
}

/** @brief Send a CS1 command; a stalled transaction requires reset before flash
 * reuse. */
static void command(unsigned cmd, int quad, unsigned read_bits,
                    uint32_t data[2])
{
    TIKU_C5_REG_WRITE(SPI1 + 8, quad ? 1u << 8 : 0);
    TIKU_C5_REG_WRITE(SPI1 + 0x34, (TIKU_C5_REG_READ(SPI1 + 0x34) & ~3u) | 1u);
    /* READ ID has a 24-bit address phase and no dummy phase. */
    TIKU_C5_REG_WRITE(SPI1 + 0x18,
                      (1u << 31) | (read_bits ? (1u << 28) | (1u << 30) : 0));
    TIKU_C5_REG_WRITE(SPI1 + 4, 0);
    TIKU_C5_REG_WRITE(SPI1 + 0x1c, read_bits ? 23u << 26 : 0);
    TIKU_C5_REG_WRITE(SPI1 + 0x20, (7u << 28) | cmd);
    TIKU_C5_REG_WRITE(SPI1 + 0x28, read_bits ? read_bits - 1u : 0);
    TIKU_C5_REG_WRITE(SPI1 + 0x58, 0);
    TIKU_C5_REG_WRITE(SPI1 + 0x5c, 0);
    TIKU_C5_REG_WRITE(SPI1, START);
    if (idle() != 0) {
        tiku_c5_fatal("PSRAM SPI1 timeout; reset required");
    }
    if (data != NULL) {
        data[0] = TIKU_C5_REG_READ(SPI1 + 0x58);
        data[1] = TIKU_C5_REG_READ(SPI1 + 0x5c);
    }
}

/** @brief Decode supported AP Memory IDs, including 8 MiB parts operating in 2T
 * mode. */
static uint32_t decode(const uint32_t data[2])
{
    unsigned density = (data[0] >> 21) & 7u;
    uint32_t eid, bytes;
    if ((data[0] & 0xffffu) != 0x5d0du || density > 2) {
        return 0;
    }
    bytes = 0x200000u << density;
    eid = __builtin_bswap32((data[0] >> 16) | (data[1] << 16));
    if (density == 2 &&
        (!(eid & (1u << 25)) || ((eid >> 5) & 0xfffffu) == 0x8a445u)) {
        bytes /= 2;
    }
    return bytes;
}

/** @brief Restore the PSRAM clock fields, pads and PMA entry saved at
 * initialization. */
static void restore(void)
{
    unsigned i;
    uint32_t spi1[11];
    uint32_t autoload = tiku_c5_rom_cache_suspend();
    uint32_t index = TIKU_C5_REG_READ(SPI0 + 0x380);
    for (i = 0; i < mapped_pages; i++) {
        TIKU_C5_REG_WRITE(SPI0 + 0x380, 128u + i);
        TIKU_C5_REG_WRITE(SPI0 + 0x37c, 0);
    }
    mapped_pages = 0;
    TIKU_C5_REG_WRITE(SPI0 + 0x380, index);
    for (i = 0; i < 11; i++) {
        spi1[i] = TIKU_C5_REG_READ(SPI1 + spi1_offsets[i]);
    }
    TIKU_C5_REG_WRITE(SPI1 + 0x14, 0x30103u);
    command(0xf5, 1, 0, NULL);
    for (i = 0; i < 11; i++) {
        TIKU_C5_REG_WRITE(SPI1 + spi1_offsets[i], spi1[i]);
    }
    TIKU_C5_PSRAM_PMA_WRITE(saved_pma, saved_address);
    for (i = 0; i < 6; i++) {
        TIKU_C5_REG_WRITE(SPI0 + spi0_offsets[i], saved_spi0[i]);
    }
    for (i = 0; i < 3; i++) {
        TIKU_C5_REG_WRITE(MUX(pads[i]), saved_mux[i]);
    }
    tiku_c5_rom_cache_resume(autoload);
}

/** @brief Test one word per MMU page with distinct addresses, then its inverse.
 */
static int verify(uint32_t bytes)
{
    unsigned pass;
    uint32_t offset;
    for (pass = 0; pass < 2; pass++) {
        for (offset = 0; offset < bytes; offset += 65536u) {
            TIKU_C5_PSRAM_WORD(offset) = (0xa5b6c789u ^ offset) ^
                                         (pass ? UINT32_MAX : 0);
        }
        TIKU_C5_PSRAM_WORD(bytes - 4u) = 0x13ace579u ^ pass;
        if (cache_writeback_invalidate(TIKU_C5_PSRAM_BASE, bytes) != 0) {
            return -1;
        }
        for (offset = 0; offset < bytes; offset += 65536u) {
            if (TIKU_C5_PSRAM_WORD(offset) !=
                ((0xa5b6c789u ^ offset) ^ (pass ? UINT32_MAX : 0))) {
                return -1;
            }
        }
        if (TIKU_C5_PSRAM_WORD(bytes - 4u) != (0x13ace579u ^ pass)) {
            return -1;
        }
    }
    return 0;
}

int tiku_c5_psram_init(void)
{
    uint32_t state, autoload, saved_spi1[11], id[2], bytes, flash_clock,
        root_clock, xtal, index;
    unsigned i;
    int result = enter(&state);
    if (result) {
        return result;
    }
    if (capacity) {
        return leave(state, 0);
    }
    if (!TIKU_C5_PSRAM_EXTERNAL && !tiku_cpu_c5_psram_package_code()) {
        return leave(state, TIKU_C5_PSRAM_ABSENT);
    }
    if (!tiku_flash_ready() || idle() != 0) {
        return leave(state, TIKU_C5_PSRAM_BUSY);
    }
    TIKU_C5_PSRAM_PMA_READ(saved_pma, saved_address);
    if (saved_pma & 0x20000001u) {
        return leave(state, TIKU_C5_PSRAM_CONFIG);
    }
    flash_clock = TIKU_C5_REG_READ(SPI0 + 0x14);
    root_clock = TIKU_C5_REG_READ(0x6009601cu) & 0xfffu;
    xtal = (TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u;
    /* Matched clocks avoid AR2026-002: ROM XTAL / 2 (20 or 24 MHz),
     * or PLL480 / 6 / 2 (40 MHz). Keep the existing flash clock source. */
    if (flash_clock != 0x10001u ||
        (root_clock != 0x605u &&
         !(root_clock == 0x400u && (xtal == 40u || xtal == 48u))) ||
        (TIKU_C5_REG_READ(SPI0 + 0xd8) & 0x60000001u) ||
        (TIKU_C5_REG_READ(SPI0 + 0x174) & (1u << 20)) ||
        (TIKU_C5_REG_READ(SPI0 + 0x1a0) & (1u << 16))) {
        return leave(state, TIKU_C5_PSRAM_CONFIG);
    }
    for (i = 0; i < 4; i++) {
        if (TIKU_C5_REG_READ(SPI0 + 0x130 + 4u * i) & 4u) {
            return leave(state, TIKU_C5_PSRAM_CONFIG);
        }
    }
    index = TIKU_C5_REG_READ(SPI0 + 0x380);
    for (i = 128; i < 256; i++) {
        TIKU_C5_REG_WRITE(SPI0 + 0x380, i);
        if (TIKU_C5_REG_READ(SPI0 + 0x37c) & (1u << 10)) {
            break;
        }
    }
    TIKU_C5_REG_WRITE(SPI0 + 0x380, index);
    if (i != 256) {
        return leave(state, TIKU_C5_PSRAM_CONFIG);
    }
    for (i = 0; i < 11; i++) {
        saved_spi1[i] = TIKU_C5_REG_READ(SPI1 + spi1_offsets[i]);
    }
    for (i = 0; i < 6; i++) {
        saved_spi0[i] = TIKU_C5_REG_READ(SPI0 + spi0_offsets[i]);
    }
    for (i = 0; i < 3; i++) {
        saved_mux[i] = TIKU_C5_REG_READ(MUX(pads[i]));
    }
    autoload = tiku_c5_rom_cache_suspend();
    for (i = 0; i < 3; i++) {
        TIKU_C5_REG_WRITE(MUX(pads[i]),
                          (saved_mux[i] & ~(7u << 12)) | (1u << 9));
    }
    TIKU_C5_REG_WRITE(SPI1 + 0x14, 0x30103u);
    command(0xf5, 1, 0, NULL);
    command(0x9f, 0, 64, id);
    if (!(bytes = decode(id))) {
        command(0x9f, 0, 64, id);
        bytes = decode(id);
    }
    device_id = id[0] & 0xffffffu;
    result = bytes ? 0
                   : (device_id == 0 || device_id == 0xffffffu
                          ? TIKU_C5_PSRAM_ABSENT
                          : TIKU_C5_PSRAM_ID);
    if (!result) {
        command(0x66, 0, 0, NULL);
        command(0x99, 0, 0, NULL);
        tiku_cpu_c5_delay_us(50);
        command(0x35, 0, 0, NULL);
        TIKU_C5_REG_WRITE(
            SPI0 + 0x40,
            (saved_spi0[0] & ~((3u << 1) | (63u << 6) | (63u << 14))) | 4u |
                16u | 32u | (5u << 6) | (23u << 14) | (1u << 20));
        TIKU_C5_REG_WRITE(SPI0 + 0x48, (7u << 28) | 0xebu);
        TIKU_C5_REG_WRITE(SPI0 + 0x4c, (7u << 28) | 0x38u);
        TIKU_C5_REG_WRITE(SPI0 + 0x50, flash_clock);
        TIKU_C5_REG_WRITE(SPI0 + 0x174,
                          (saved_spi0[4] & ~(3u << 18)) |
                              ((bytes == 0x200000u ? 1u : 2u) << 18));
        TIKU_C5_REG_WRITE(SPI0 + 0x1a0, saved_spi0[5] | (1u << 31) | 3u);
        TIKU_C5_PSRAM_PMA_WRITE(0xc0000019u,
                                (TIKU_C5_PSRAM_BASE | (bytes / 2u - 1u)) >> 2);
        mapped_pages = bytes / 65536u;
        if (tiku_c5_rom_mmu_set(0, 1u << 9, TIKU_C5_PSRAM_BASE, 0, 64,
                                bytes / 65536u, 0) != 0) {
            result = TIKU_C5_PSRAM_MAP;
        }
    }
    for (i = 0; i < 11; i++) {
        TIKU_C5_REG_WRITE(SPI1 + spi1_offsets[i], saved_spi1[i]);
    }
    tiku_c5_rom_cache_resume(autoload);
    if (!result &&
        (tiku_c5_rom_cache_invalidate(TIKU_C5_PSRAM_BASE, bytes) != 0 ||
         verify(bytes) != 0)) {
        result = TIKU_C5_PSRAM_VERIFY;
    }
    if (result) {
        if (bytes) {
            (void)tiku_c5_rom_cache_invalidate(TIKU_C5_PSRAM_BASE, bytes);
        }
        restore();
    } else {
        capacity = bytes;
    }
    return leave(state, result);
}

/** @brief Distinguish this mapping, a detached tier and a replacement tier. */
static int attachment(void)
{
    const uint8_t *base;
    tiku_mem_stats_t stats;
    if (tiku_tier_span_stats(TIKU_MEM_PSRAM, 0, &base, &stats) != TIKU_MEM_OK) {
        return 0;
    }
    return (uintptr_t)base == TIKU_C5_PSRAM_BASE &&
                   stats.total_bytes == capacity
               ? 1
               : -1;
}

int tiku_c5_psram_attach(void)
{
    uint32_t state;
    int result = tiku_c5_psram_init();
    if (result) {
        return result;
    }
    result = enter(&state);
    if (result) {
        return result;
    }
    if (attached && attachment() == 0) {
        attached = 0;
    }
    if (attachment() < 0) {
        return leave(state, TIKU_C5_PSRAM_BUSY);
    }
    if (!attached) {
        if (tiku_tier_attach_psram((void *)(uintptr_t)TIKU_C5_PSRAM_BASE,
                                   capacity) != TIKU_MEM_OK) {
            return leave(state, TIKU_C5_PSRAM_BUSY);
        }
        attached = 1;
    }
    return leave(state, 0);
}

int tiku_c5_psram_down(void)
{
    uint32_t state;
    int result = enter(&state);
    if (result) {
        return result;
    }
    if (capacity && attachment() < 0) {
        return leave(state, TIKU_C5_PSRAM_BUSY);
    }
    if (attached && tiku_tier_detach_psram(0) != TIKU_MEM_OK) {
        return leave(state, TIKU_C5_PSRAM_BUSY);
    }
    attached = 0;
    if (capacity) {
        (void)tiku_c5_rom_cache_invalidate(TIKU_C5_PSRAM_BASE, capacity);
        restore();
        capacity = 0;
    }
    return leave(state, 0);
}

int tiku_c5_psram_sync(const void *address, uint32_t length)
{
    uintptr_t a = (uintptr_t)address;
    uint32_t state, result;
    state = TIKU_C5_IRQ_SAVE();
    if (a < TIKU_C5_PSRAM_BASE || length == 0 || length > capacity ||
        a - TIKU_C5_PSRAM_BASE > capacity - length) {
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_C5_PSRAM_CONFIG;
    }
    result = cache_writeback_invalidate((uint32_t)a & ~31u,
                                        ((a & 31u) + length + 31u) & ~31u);
    TIKU_C5_IRQ_RESTORE(state);
    return result ? TIKU_C5_PSRAM_IO : 0;
}
uint32_t tiku_c5_psram_size(void)
{
    return capacity;
}
uint32_t tiku_c5_psram_id(void)
{
    return device_id;
}
int tiku_c5_psram_result(void)
{
    return last_result;
}
int tiku_c5_psram_attached(void)
{
    return attached && attachment() == 1;
}
