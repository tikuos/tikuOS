/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_arch.c - C6 CPU identity, clocks and interrupt controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_io_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_i2c_arch.h"
#include "tiku_spi_arch.h"
#include "tiku_cpu_common.h"
#include <hal/tiku_cpu.h>
#include <hal/tiku_wake_hal.h>
#include <stddef.h>
#ifndef TIKU_C6_ROM_CPU_MHZ
#define TIKU_C6_ROM_CPU_MHZ() ((unsigned (*)(void))(uintptr_t)0x40000044u)()
#endif
#ifndef TIKU_C6_ROM_RESET_REASON
#define TIKU_C6_ROM_RESET_REASON() ((unsigned (*)(int))(uintptr_t)0x40000018u)(0)
#endif
#ifndef TIKU_C6_INSTRUCTION_SYNC
#define TIKU_C6_INSTRUCTION_SYNC() __asm__ volatile("fence.i" ::: "memory")
#endif
void tiku_cpu_boot_init(void)
{
    tiku_c6_i2c_boot();
    tiku_c6_spi_boot();
}
void tiku_cpu_irq_enable(void)
{
    native_irq_restore(8u);
}
void tiku_cpu_irq_disable(void)
{
    (void)native_irq_save();
}
unsigned long tiku_cpu_mclk_hz(void)
{
    return (unsigned long)TIKU_C6_ROM_CPU_MHZ() * 1000000UL;
}
unsigned long tiku_cpu_smclk_hz(void)
{
    return 80000000UL;
}
unsigned long tiku_cpu_aclk_hz(void)
{
    return 0;
}
int tiku_cpu_clock_has_fault(void)
{
    return native_errors != 0;
}
void tiku_cpu_freq_init(unsigned int mhz)
{
    (void)mhz;
}
const char *tiku_cpu_freq_change_mode(void)
{
    return "fixed";
}
unsigned long tiku_cpu_freq_available(unsigned int i)
{
    return i ? 0 : tiku_cpu_mclk_hz();
}
unsigned long tiku_cpu_freq_target_hz(void)
{
    return (unsigned long)TIKU_C6_ROM_CPU_MHZ() * 1000000UL;
}
int tiku_cpu_freq_target_set(unsigned long hz)
{
    return hz == tiku_cpu_mclk_hz() ? 0 : -1;
}
void tiku_cpu_freq_boot_apply(void)
{
}
void tiku_cpu_freq_boot_set(unsigned long hz)
{
    (void)hz;
}
void tiku_cpu_dcache_clean(const void *p, unsigned long n)
{
    (void)p;
    (void)n;
}
void tiku_cpu_dcache_invalidate(const void *p, unsigned long n)
{
    (void)p;
    (void)n;
}
void tiku_cpu_icache_invalidate(void)
{
    TIKU_C6_INSTRUCTION_SYNC();
}
int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t m)
{
    return m == TIKU_CPU_IDLE_LIGHT;
}
unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t m)
{
    return m == TIKU_CPU_IDLE_LIGHT
               ? TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER | TIKU_WAKE_UART_RX | TIKU_WAKE_GPIO
               : 0;
}
const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t m)
{
    return m == TIKU_CPU_IDLE_OFF ? "off" : m == TIKU_CPU_IDLE_LIGHT ? "WFI" : "unsupported";
}
const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t m)
{
    return tiku_cpu_idle_mode_name(m);
}
void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    if (!out)
        return;
    *out = (tiku_wake_sources_t){0};
    uint32_t enabled = TIKU_C6_REG_READ(C6_PLIC), control = TIKU_C6_REG_READ(C6_SYSTIMER);
    uint32_t timer_irq = TIKU_C6_REG_READ(C6_SYSTIMER + 0x64);
    if ((enabled & (1u << 17)) && (control & (1u << 23)) && (timer_irq & 2u))
        out->sources |= TIKU_WAKE_SYSTICK;
    if ((enabled & (1u << 19)) && (control & (1u << 24)) && (timer_irq & 1u))
        out->sources |= TIKU_WAKE_HTIMER;
    if ((enabled & (1u << 21)) && TIKU_C6_REG_READ(0x6000000cu))
        out->sources |= TIKU_WAKE_UART_RX;
    uint32_t armed = tiku_c6_gpio_irq_armed();
    if ((enabled & (1u << 20)) && armed) {
        out->sources |= TIKU_WAKE_GPIO;
        for (unsigned i = 0; i < 4; i++)
            out->gpio_ie[i] = (uint8_t)(armed >> (8 * i));
    }
}
void tiku_cpu_c6_delay_us(unsigned int us)
{
    while (us) {
        unsigned n = us > 1000 ? 1000 : us;
        uint64_t t = native_time();
        while (native_time() - t < (uint64_t)n * 16) {
        }
        us -= n;
    }
}
void tiku_cpu_c6_delay_ms(unsigned int ms)
{
    while (ms--)
        tiku_cpu_c6_delay_us(1000);
}
uint8_t tiku_cpu_c6_unique_id(uint8_t *out, uint8_t n)
{
    if (!out)
        return 0;
    if (n > 6)
        n = 6;
    uint32_t low = TIKU_C6_REG_READ(0x600b0844u), high = TIKU_C6_REG_READ(0x600b0848u);
    for (uint8_t i = 0; i < n; i++) {
        unsigned j = 5 - i;
        out[i] = (uint8_t)((j < 4 ? low : high) >> (8 * (j % 4)));
    }
    return n;
}
uint16_t tiku_cpu_c6_reset_reason(void)
{
    switch (TIKU_C6_ROM_RESET_REASON()) {
    case 3:
    case 12:
        return 0x06;
    case 5:
        return 0x08;
    case 1:
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
void tiku_cpu_c6_restart(void)
{
    (void)native_irq_save();
    ((void (*)(void))(uintptr_t)0x40000090)();
    for (;;) {
    }
}

uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)__native_stack_bottom;
}
void tiku_hang_arch_reset(void)
{
    tiku_cpu_c6_restart();
}
