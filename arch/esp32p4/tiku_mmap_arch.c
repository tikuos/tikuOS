/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mmap_arch.c - P4 SPI0 read mapping; ECO2 cache control and MMU readback.
 *
 * Maps the 16 MiB flash at 0x40000000 as 256 64 KiB MMU pages, reading back
 * all 1024 entries, and invalidates the cache over the window.  A failed
 * map or unmap leaves both calls refusing until reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_mmap_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

#define MAP_SPI0 0x5008c000u
#define MAP_CLOCK (MAP_SPI0 + 0x14u)
#define MAP_AXI (MAP_SPI0 + 0x3cu)
#define MAP_CONTENT (MAP_SPI0 + 0x37cu)
#define MAP_INDEX (MAP_SPI0 + 0x380u)
#define MAP_VALID (1u << 12)
#define MAP_CLOSE (1u << 31)
#define MAP_CACHES 0x31u
#ifndef TIKU_P4_MAP_MODE
#define TIKU_P4_MAP_MODE() ((int (*)(unsigned))0x4fc00184u)(5)
#define TIKU_P4_MAP_SUSPEND() ((uint32_t (*)(void))0x4fc00508u)()
#define TIKU_P4_MAP_ENABLE() ((void (*)(unsigned))0x4fc00504u)(0)
#define TIKU_P4_MAP_INVALIDATE() \
    ((int (*)(unsigned, uint32_t, uint32_t))0x4fc003e4u) \
        (MAP_CACHES, TIKU_P4_FLASH_VIRTUAL, TIKU_P4_FLASH_SIZE)
#define TIKU_P4_MAP_BYTE(a) (*(volatile const uint8_t *)(uintptr_t)(a))
#define TIKU_P4_MAP_FENCE() __asm__ volatile("fence iorw, iorw\nfence.i" ::: "memory")
#endif
static uint8_t mapped, failed;

/** @brief Require both SPI0 state machines to become idle within a fixed bound.
 */
static int idle(void)
{
    unsigned tries;
    for (tries = 0; tries < 65536; tries++)
        if (!(TIKU_P4_REG_READ(MAP_SPI0) & 255u)) return 1;
    return 0;
}

/** @brief Set and read back each of the 1024 entries, excluding absent flash.
 */
static int entries(int enable)
{
    unsigned page;
    for (page = 0; page < 1024; page++) {
        uint32_t value = enable && page < 256 ? MAP_VALID | page : 0;
        TIKU_P4_REG_WRITE(MAP_INDEX, page);
        TIKU_P4_REG_WRITE(MAP_CONTENT, value);
        if (TIKU_P4_REG_READ(MAP_CONTENT) != value) return 0;
    }
    return 1;
}

int tiku_p4_mmap_init(void)
{
    uint32_t state;
    int ok;
    if (tiku_p4_in_isr() || failed) return -1;
    if (mapped) return 0;
    if (!tiku_p4_cache_layout_ok() || tiku_p4_flash_init()) return -1;
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_MAP_SUSPEND();
    TIKU_P4_REG_WRITE(MAP_AXI, TIKU_P4_REG_READ(MAP_AXI) | MAP_CLOSE);
    ok = idle() && !TIKU_P4_MAP_MODE();
    if (ok) {
        TIKU_P4_REG_WRITE(MAP_CLOCK, 0x00030103u);
        ok = TIKU_P4_REG_READ(MAP_CLOCK) == 0x00030103u && entries(1) &&
             !TIKU_P4_MAP_INVALIDATE();
    }
    if (ok) {
        TIKU_P4_REG_WRITE(MAP_AXI, TIKU_P4_REG_READ(MAP_AXI) & ~MAP_CLOSE);
        TIKU_P4_MAP_ENABLE();
        TIKU_P4_MAP_FENCE();
        mapped = 1;
    } else {
        failed = 1;
    }
    TIKU_P4_IRQ_RESTORE(state);
    return ok ? 0 : -1;
}

int tiku_p4_mmap_shutdown(void)
{
    uint32_t state;
    int ok;
    if (tiku_p4_in_isr() || failed) return -1;
    if (!mapped) return 0;
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_MAP_SUSPEND();
    TIKU_P4_REG_WRITE(MAP_AXI, TIKU_P4_REG_READ(MAP_AXI) | MAP_CLOSE);
    ok = idle() && !TIKU_P4_MAP_INVALIDATE() && entries(0);
    mapped = 0;
    if (!ok) failed = 1;
    TIKU_P4_MAP_FENCE();
    TIKU_P4_IRQ_RESTORE(state);
    return ok ? 0 : -1;
}

int tiku_p4_mmap_ready(void) { return mapped; }

int tiku_p4_mmap_read(uint32_t offset, void *data, uint32_t length)
{
    uint8_t *bytes = data;
    uint32_t i;
    if (!data || offset > TIKU_P4_FLASH_SIZE || length > TIKU_P4_FLASH_SIZE - offset)
        return TIKU_P4_FLASH_PARAM;
    if (!mapped) return TIKU_P4_FLASH_DOWN;
    for (i = 0; i < length; i++) bytes[i] = TIKU_P4_MAP_BYTE(TIKU_P4_FLASH_VIRTUAL + offset + i);
    return 0;
}
