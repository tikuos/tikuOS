/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_clock_arch.c - C5 CPU dividers with a constant 40 MHz AHB clock.
 * Register fields and divider constraints follow ESP-IDF 4d59230,
 * esp32c5 clk_tree_ll.h and rtc_clk.c. The PLL source stays unchanged; the
 * calibrated core voltage is applied before the first divider change.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_cpu_settings.h>
#include "tiku_clock_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_rom_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_analog_arch.h"
#include "tiku_pmu_arch.h"

#define C5_PCR_SYSCLK       0x60096110UL
#define C5_PCR_CPU          0x60096118UL
#define C5_PCR_AHB          0x6009611CUL
#define C5_PCR_UPDATE       0x60096144UL
#define C5_CLOCK_SPINS      4096u

static int clock_fault;

/** @brief Require PLL240, AHB /6 and a supported CPU divider with no pending update. */
static int clock_tree_ready(void)
{
    unsigned divider = (TIKU_C5_REG_READ(C5_PCR_CPU) & 255u) + 1u;
    return ((TIKU_C5_REG_READ(C5_PCR_SYSCLK) >> 16) & 3u) == 3u &&
           (TIKU_C5_REG_READ(C5_PCR_AHB) & 255u) == 5u &&
           !(TIKU_C5_REG_READ(C5_PCR_UPDATE) & 1u) &&
           (divider == 1u || divider == 3u || divider == 6u);
}

/** @brief Latch a divider and wait a bounded number of reads for completion. */
static int apply_divider(uint32_t value)
{
    unsigned i;
    TIKU_C5_REG_WRITE(C5_PCR_CPU, value);
    TIKU_C5_REG_WRITE(C5_PCR_UPDATE, TIKU_C5_REG_READ(C5_PCR_UPDATE) | 1u);
    for (i = 0; i < C5_CLOCK_SPINS; i++) {
        if (!(TIKU_C5_REG_READ(C5_PCR_UPDATE) & 1u)) {
            return TIKU_C5_REG_READ(C5_PCR_CPU) == value ? 0 : -1;
        }
    }
    return -1;
}

int tiku_c5_clock_set(unsigned long hz)
{
    uint32_t state, previous, desired;
    unsigned mhz;
    if (hz != 40000000UL && hz != 80000000UL && hz != 240000000UL) { return -1; }
    /* 240 MHz needs the calibrated regulator setting; without it only the
     * slower dividers are applied. */
    if (tiku_c5_core_voltage_ready() != 0 && hz > 80000000UL) { return -1; }
    state = TIKU_C5_IRQ_SAVE();
    if (!clock_tree_ready() || tiku_c5_analog_owner() == TIKU_C5_ANALOG_PHY) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    previous = TIKU_C5_REG_READ(C5_PCR_CPU);
    mhz = (unsigned)(hz / 1000000UL);
    desired = (previous & ~255u) | (240u / mhz - 1u);
    if (desired != previous && apply_divider(desired) != 0) {
        clock_fault = 1;
        if (apply_divider(previous) != 0) { tiku_c5_fatal("clock rollback failed"); }
        tiku_c5_rom_cpu_frequency_set(240u / ((previous & 255u) + 1u));
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    tiku_c5_rom_cpu_frequency_set(mhz);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

int tiku_c5_clock_fault(void) { return clock_fault; }
void tiku_cpu_freq_init(unsigned int mhz)
{
    if (mhz == 40u || mhz == 80u || mhz == 240u) {
        (void)tiku_c5_clock_set((unsigned long)mhz * 1000000UL);
    }
}
const char *tiku_cpu_freq_change_mode(void)
{
    return tiku_cpu_freq_available(1) ? "reboot" : "fixed";
}
unsigned long tiku_cpu_freq_available(unsigned int index)
{
    static const unsigned long rates[] = { 40000000UL, 80000000UL, 240000000UL };
    if (!clock_tree_ready()) { return index ? 0 : tiku_cpu_mclk_hz(); }
    return index < sizeof rates / sizeof rates[0] ? rates[index] : 0;
}
unsigned long tiku_cpu_freq_target_hz(void) { return tiku_cpu_settings_target(); }
int tiku_cpu_freq_target_set(unsigned long hz) { return tiku_cpu_settings_save(hz); }
void tiku_cpu_freq_boot_apply(void) { tiku_cpu_settings_boot(); }
void tiku_cpu_freq_boot_set(unsigned long hz) { (void)tiku_c5_clock_set(hz); }
