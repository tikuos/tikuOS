/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_cpu_arch.c - C5 CPU HAL, interrupt state, identity and shallow idle.
 * HP SRAM is uncached; mapped PSRAM has an explicit synchronization API.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <hal/tiku_cpu.h>
#include <hal/tiku_wake_hal.h>
#include "tiku_cpu_common.h"
#include "tiku_rom_arch.h"
#include "tiku_boot_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_systimer_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_debug_arch.h"
#include "tiku_clock_arch.h"
#if TIKU_ESP32C5_XIP_CODE
#include "tiku_flash_arch.h"
#include "tiku_xip_arch.h"
#endif

static unsigned nesting;
static uint32_t outer_state;

void tiku_atomic_enter(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (nesting++ == 0) {
        outer_state = state;
    }
}
void tiku_atomic_exit(void)
{
    if (nesting == 0) {
        tiku_c5_fatal("unbalanced atomic exit");
    }
    if (--nesting == 0) {
        TIKU_C5_IRQ_RESTORE(outer_state);
    }
}
void tiku_cpu_irq_enable(void)
{
    TIKU_C5_IRQ_RESTORE(8u);
}
void tiku_cpu_irq_disable(void)
{
    (void)TIKU_C5_IRQ_SAVE();
}
void tiku_cpu_boot_init(void)
{
    tiku_esp32c5_boot_watchdogs_disable();
    if (TIKU_C5_REG_READ(TIKU_C5_ROM_CHIP_ID) != TIKU_ESP32C5_IMAGE_CHIP_ID ||
        TIKU_C5_REG_READ(TIKU_C5_ROM_ECO) < TIKU_ESP32C5_MIN_ROM_ECO) {
        tiku_c5_fatal("unsupported ROM identity");
    }
    /* The UART, I2C and watchdog divisors assume a 48 MHz crystal; PCR
     * SYSCLK_CONF bits 30:24 hold the crystal frequency in MHz. */
    if (((TIKU_C5_REG_READ(0x60096110u) >> 24) & 127u) != 48u) {
        tiku_c5_fatal("crystal is not 48 MHz");
    }
    tiku_c5_irq_init();
#if TIKU_ESP32C5_XIP_CODE
    if (tiku_flash_init() != TIKU_FLASH_OK) {
        tiku_c5_fatal("flash mapping failed");
    }
    tiku_c5_xip_require();
#endif
}
unsigned long tiku_cpu_mclk_hz(void)
{
    return tiku_c5_rom_cpu_frequency() * 1000000UL;
}
unsigned long tiku_cpu_smclk_hz(void)
{
    return tiku_cpu_mclk_hz();
}
unsigned long tiku_cpu_aclk_hz(void)
{
    return 0;
}
int tiku_cpu_clock_has_fault(void)
{
    return tiku_c5_clock_fault() || tiku_c5_systimer_errors() != 0;
}
void tiku_cpu_dcache_clean(const void *address, unsigned long length)
{
    (void)address;
    (void)length;
}
void tiku_cpu_dcache_invalidate(const void *address, unsigned long length)
{
    (void)address;
    (void)length;
}
void tiku_cpu_icache_invalidate(void)
{
    __asm__ volatile("fence.i" ::: "memory");
}

/** @brief Wait for an enabled interrupt without powering down USB or its PHY.
 */
static void idle(void)
{
    tiku_debug_arch_poll();
    __asm__ volatile("wfi" ::: "memory");
}
tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode)
{
    return mode >= TIKU_CPU_IDLE_LIGHT && mode <= TIKU_CPU_IDLE_DEEPEST ? idle
                                                                        : NULL;
}
int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode)
{
    (void)mode;
    return 1;
}
unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t mode)
{
    (void)mode;
    return TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER | TIKU_WAKE_UART_RX;
}
const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode) ? "WFI" : "off";
}
const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode)
{
    return tiku_cpu_idle_hook(mode)
               ? "WFI (USB powered; timer or enabled UART wake)"
               : "off (busy-wait)";
}

/** @brief Read the low 32 bits of the CPU cycle counter. */
static uint32_t cycles(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}
void tiku_cpu_c5_delay_us(unsigned int us)
{
    uint32_t mhz = tiku_c5_rom_cpu_frequency();
    while (us) {
        uint32_t part = us > 1000000u ? 1000000u : us;
        uint32_t start = cycles();
        while ((uint32_t)(cycles() - start) < part * mhz) {
        }
        us -= part;
    }
}
void tiku_cpu_c5_delay_ms(unsigned int ms)
{
    while (ms--) {
        tiku_cpu_c5_delay_us(1000);
    }
}
uint8_t tiku_cpu_c5_unique_id(uint8_t *data, uint8_t length)
{
    uint32_t low, high;
    unsigned i;
    if (data == NULL) {
        return 0;
    }
    if (length > 6) {
        length = 6;
    }
    low = TIKU_C5_REG_READ(0x600B4844u);
    high = TIKU_C5_REG_READ(0x600B4848u);
    for (i = 0; i < length; i++) {
        data[i] = (uint8_t)(i < 2 ? high >> ((1u - i) * 8u)
                                  : low >> ((5u - i) * 8u));
    }
    return length;
}
unsigned tiku_cpu_c5_psram_package_code(void)
{
    /* ESP-IDF 4d59230 C5 eFuse block 1, bits 83..85. */
    return (TIKU_C5_REG_READ(0x600B484Cu) >> 19) & 7u;
}

uint16_t tiku_cpu_c5_reset_reason(void)
{
    uint32_t reason = tiku_c5_rom_reset_reason(0);
    /* ESP-IDF 4d59230 C5 rom/rtc.h supplies the raw reset codes. The
     * common SYSRSTIV rendering has no distinct power-on value. */
    switch (reason) {
    case 3:
    case 12:
        return 0x06;
    case 5:
        return 0x08;
    case 15:
        return 0x02;
    case 21:
    case 22:
    case 24:
        return 0x14;
    case 7:
    case 8:
    case 9:
    case 11:
    case 13:
    case 16:
    case 17:
    case 18:
        return 0x16;
    default:
        return 0;
    }
}
void tiku_cpu_c5_restart(void)
{
    (void)TIKU_C5_IRQ_SAVE();
    (void)tiku_c5_rom_cache_suspend();
    tiku_c5_rom_reset();
}

#if !TIKU_THREADS_ENABLE
#include <reent.h>
/**
 * @brief The C library's reentrancy state, for errno and stdio.
 *
 * Espressif's newlib resolves every errno and stdio access through this
 * call.  With workers the thread backend defines it; without them the
 * kernel state is the only one.
 */
struct _reent *__getreent(void)
{
    return _impure_ptr;
}
#endif
