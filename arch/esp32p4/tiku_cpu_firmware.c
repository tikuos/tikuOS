/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_firmware.c - P4 CPU0 boot, frequency preferences and identity.
 *
 * Boots CPU0 (watchdogs, ROM check, IRQs, clocks, TRNG, flash, XIP code),
 * applies the saved CPU rate at boot and syncs PSRAM data cache lines.
 * ROM and eFuse addresses: ESP-IDF 4d59230, ESP32-P4 hw_ver1/ECO2.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_cpu_settings.h>
#include "tiku_cpu_common.h"
#include "tiku_boot_arch.h"
#include "tiku_clock_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_xip_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_debug_arch.h"
#include "tiku_trng_arch.h"
#include <services/console/tiku_usb_serial_jtag.h>

__attribute__((used, section(".p4.firmware")))
const char tiku_p4_firmware_identity[] = "TIKU-P4-FIRMWARE-v1:00400000:00bf8000";

void tiku_p4_fatal(const char *reason)
{
    (void)TIKU_P4_IRQ_SAVE();
    tiku_debug_arch_printf("P4 FATAL: %s\n", reason);
    for (;;) tiku_serial_jtag_poll();
}

void tiku_cpu_boot_init(void)
{
    tiku_esp32p4_boot_watchdogs_disable();
    if (TIKU_P4_REG_READ(TIKU_P4_ROM_CHIP_ID) != 18u ||
        TIKU_P4_REG_READ(TIKU_P4_ROM_ECO) != 2u)
        tiku_p4_fatal("unsupported ROM identity");
    tiku_p4_irq_init();
    if (tiku_p4_clock_pll_init() || tiku_p4_systimer_init())
        tiku_p4_fatal("CPU clock initialization");
    tiku_trng_arch_init();
    if (tiku_p4_flash_init() != TIKU_P4_FLASH_OK || tiku_p4_xip_init())
        tiku_p4_fatal("flash executable companion");
}

unsigned long tiku_cpu_mclk_hz(void)
{
    unsigned root = TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u;
    unsigned div = ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV0) >> 5) & 255u) + 1u;
    return (root == 1u ? 360000000UL : 40000000UL) / div;
}

unsigned long tiku_cpu_smclk_hz(void)
{
    unsigned mem = (TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV1) & 255u) + 1u;
    unsigned apb = ((TIKU_P4_REG_READ(TIKU_P4_ROOT_DIV2) >> 16) & 255u) + 1u;
    return tiku_cpu_mclk_hz() / mem / apb;
}

unsigned long tiku_cpu_aclk_hz(void) { return 0; }
int tiku_cpu_clock_has_fault(void)
{
    return (TIKU_P4_REG_READ(TIKU_P4_ROOT_SELECT) & 3u) > 1u;
}

void tiku_cpu_freq_init(unsigned int mhz)
{
    if (mhz != 40 && mhz != 90 && mhz != 180 && mhz != 360) return;
    if (tiku_p4_clock_set_mhz(mhz)) tiku_p4_fatal("CPU frequency");
}
unsigned long tiku_cpu_freq_available(unsigned int index)
{
    static const unsigned long rates[] = {40000000, 90000000, 180000000, 360000000};
    return index < sizeof rates / sizeof rates[0] ? rates[index] : 0;
}
const char *tiku_cpu_freq_change_mode(void) { return "reboot"; }
unsigned long tiku_cpu_freq_target_hz(void) { return tiku_cpu_settings_target(); }
int tiku_cpu_freq_target_set(unsigned long hz) { return tiku_cpu_settings_save(hz); }
void tiku_cpu_freq_boot_apply(void) { tiku_cpu_settings_boot(); }
void tiku_cpu_freq_boot_set(unsigned long hz)
{
    if (hz % 1000000UL || tiku_p4_clock_set_mhz(hz / 1000000UL))
        tiku_p4_fatal("saved CPU frequency");
}

void tiku_cpu_p4_delay_us(unsigned int us) { tiku_clock_arch_delay(us); }
void tiku_cpu_p4_delay_ms(unsigned int ms)
{
    while (ms--) tiku_cpu_p4_delay_us(1000);
}
uint8_t tiku_cpu_p4_unique_id(uint8_t *data, uint8_t length)
{
    uint32_t low, high;
    unsigned i;
    if (!data) return 0;
    if (length > 6) length = 6;
    low = TIKU_P4_REG_READ(0x5012d044u);
    high = TIKU_P4_REG_READ(0x5012d048u);
    for (i = 0; i < length; i++)
        data[i] = (uint8_t)(i < 2 ? high >> ((1u - i) * 8u)
                                 : low >> ((5u - i) * 8u));
    return length;
}
uint16_t tiku_cpu_p4_reset_reason(void)
{
    unsigned cause = ((unsigned (*)(unsigned))0x4fc00018u)(0);
    switch (cause) {
    case 3: case 12: return 0x06;
    case 5: return 0x08;
    case 15: return 0x02;
    case 7: case 8: case 9: case 11: case 13: case 16: case 17: case 18:
        return 0x16;
    default: return 0;
    }
}
/** @brief Run a bounded cache command, halting rather than returning stale data. */
static void cache_sync(uint32_t map, uint32_t start, uint32_t size, uint32_t op)
{
    unsigned tries;
    TIKU_P4_REG_WRITE(0x3ff1009cu, map);
    TIKU_P4_REG_WRITE(0x3ff100a0u, start);
    TIKU_P4_REG_WRITE(0x3ff100a4u, size);
    TIKU_P4_REG_WRITE(0x3ff10098u, op);
    for (tries = 0; tries < 1000000u; tries++)
        if (TIKU_P4_REG_READ(0x3ff10098u) & 16u) return;
    tiku_p4_fatal("cache synchronization");
}
/** @brief Synchronize aligned PSRAM lines without losing adjacent dirty bytes. */
static void psram_sync(const void *address, unsigned long length, int invalidate)
{
    uintptr_t start = (uintptr_t)address, end;
    uint32_t state;
    if (!length || start < TIKU_P4_PSRAM_BASE ||
        start >= TIKU_P4_PSRAM_BASE + TIKU_P4_PSRAM_BYTES) return;
    if (length > TIKU_P4_PSRAM_BASE + TIKU_P4_PSRAM_BYTES - start)
        tiku_p4_fatal("cache range");
    end = (start + length + 63u) & ~63u;
    start &= ~63u;
    state = TIKU_P4_IRQ_SAVE();
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    cache_sync(0x10u, start, end - start, 4u);
    cache_sync(0x10u, start, end - start, 4u);
    cache_sync(0x20u, start, end - start, 4u);
    if (invalidate) cache_sync(0x30u, start, end - start, 1u);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    TIKU_P4_IRQ_RESTORE(state);
}
void tiku_cpu_p4_restart(void)
{
    (void)TIKU_P4_IRQ_SAVE();
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    cache_sync(0x10u, TIKU_P4_SRAM_LOW_START,
               TIKU_P4_SRAM_HIGH_END - TIKU_P4_SRAM_LOW_START, 4u);
    ((void (*)(void))0x4fc00094u)();
    for (;;) __asm__ volatile("wfi");
}
void tiku_cpu_dcache_clean(const void *address, unsigned long length)
{
    psram_sync(address, length, 0);
}
void tiku_cpu_dcache_invalidate(const void *address, unsigned long length)
{
    psram_sync(address, length, 1);
}
void tiku_cpu_icache_invalidate(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    cache_sync(1u, 0, 0, 1u);
    __asm__ volatile("fence.i" ::: "memory");
    TIKU_P4_IRQ_RESTORE(state);
}
