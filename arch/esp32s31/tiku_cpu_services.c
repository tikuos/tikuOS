/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_services.c - S31 CPU identity, clocks and interrupt controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_cpu_common.h"
#include "tiku_runtime_arch.h"
#include "tiku_clock_arch.h"
#include <kernel/threads/tiku_thread.h>
#include <hal/tiku_cpu.h>
#include <hal/tiku_wake_hal.h>
#include <hal/tiku_clock_hal.h>
#include <stddef.h>
extern unsigned rtc_get_reset_reason(int cpu);
extern void software_reset(void);
extern uint8_t __tier_sram_end[];
void tiku_cpu_boot_init(void) { }
unsigned long tiku_cpu_mclk_hz(void)
{
    return tiku_s31_clock_mhz() * 1000000UL;
}
unsigned long tiku_cpu_smclk_hz(void)
{
    return tiku_s31_clock_apb_hz();
}
unsigned long tiku_cpu_aclk_hz(void)
{
    return 0;
}
int tiku_cpu_clock_has_fault(void)
{
    return tiku_s31_clock_fault();
}
/* The running rate changes on the kernel thread only; the build's rate is
 * the boot rate, and an unoffered rate changes nothing. */
void tiku_cpu_freq_init(unsigned int mhz)
{
    if (tiku_thread_in_kernel() && !tiku_s31_in_trap) (void)tiku_s31_clock_set(mhz);
}
unsigned long tiku_s31_clock_measure_hz(void)
{
    uint64_t start = tiku_s31_time(), now;
    uint32_t c0, c1;

    __asm__ volatile ("csrr %0, mcycle" : "=r"(c0));
    do {
        now = tiku_s31_time();
    } while (now - start < 160000u);
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c1));
    return (unsigned long)((uint64_t)(c1 - c0) * 16000000u / (now - start));
}
/* "fixed": the next boot runs the build's rate; freq_init() changes the
 * running one. */
const char *tiku_cpu_freq_change_mode(void)
{
    return "fixed";
}
unsigned long tiku_cpu_freq_available(unsigned int i)
{
    static const unsigned mhz[] = { 40u, 80u, 160u, 320u };

    for (unsigned k = 0; k < sizeof mhz / sizeof mhz[0]; k++) {
        if (tiku_s31_clock_offers(mhz[k]) && i-- == 0u) return mhz[k] * 1000000UL;
    }
    return 0;
}
unsigned long tiku_cpu_freq_target_hz(void)
{
    return TIKU_S31_CPU_MHZ * 1000000UL;
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
unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t m)
{
    return m == TIKU_CPU_IDLE_LIGHT
               ? TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER | TIKU_WAKE_UART_RX
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
    if (!out) return;
    *out = (tiku_wake_sources_t){0};
    volatile uint32_t *clic = (volatile uint32_t *)0x10801000u;
    uint32_t timer = *(volatile uint32_t *)0x20399064u;
    if ((clic[17] & 256u) && (timer & 1u)) out->sources |= TIKU_WAKE_SYSTICK;
    if ((clic[18] & 256u) && (timer & 2u)) out->sources |= TIKU_WAKE_HTIMER;
    if ((clic[19] & 256u) && *(volatile uint32_t *)0x2038a00cu)
        out->sources |= TIKU_WAKE_UART_RX;
}
void tiku_cpu_s31_delay_us(unsigned int us) { tiku_clock_arch_delay(us); }
void tiku_cpu_s31_delay_ms(unsigned int ms)
{
    while (ms--)
        tiku_cpu_s31_delay_us(1000);
}
uint8_t tiku_cpu_s31_unique_id(uint8_t *out, uint8_t n)
{
    if (!out)
        return 0;
    if (n > 6)
        n = 6;
    uint32_t low = *(volatile uint32_t *)0x20715050u, high = *(volatile uint32_t *)0x20715054u;
    for (uint8_t i = 0; i < n; i++) {
        unsigned j = 5 - i;
        out[i] = (uint8_t)((j < 4 ? low : high) >> (8 * (j % 4)));
    }
    return n;
}
uint16_t tiku_cpu_s31_reset_reason(void)
{
    switch (rtc_get_reset_reason(0)) {
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
void tiku_cpu_s31_restart(void)
{
    tiku_cpu_irq_disable();
    software_reset();
    for (;;) {
    }
}

uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)__tier_sram_end;
}
void tiku_hang_arch_reset(void)
{
    tiku_cpu_s31_restart();
}
