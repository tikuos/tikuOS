/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_timer_arch.c - C5 system clock, one-shot timer and critical-window masks.
 * SYSTIMER alarm 0 drives ticks, alarm 1 drives the kernel hardware timer.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_timer_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"
#include <kernel/timers/tiku_htimer.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_crit.h>
#include <kernel/scheduler/tiku_sched.h>
#include <hal/tiku_crit_hal.h>
#include <hal/tiku_wake_hal.h>
#include "tiku_cpu_common.h"

static uint64_t epoch;
static unsigned long seconds_offset;
static uint32_t saved_enables;

/** @brief Read the timebase or halt when the counter cannot be sampled. */
static uint64_t now(void)
{
    uint64_t value;
    if (tiku_c5_systimer_read(&value) != 0) { tiku_c5_fatal("SYSTIMER read timeout"); }
    return value;
}

void tiku_c5_tick_notify(void) { tiku_sched_notify(); }

void tiku_clock_arch_init(void)
{
    if (tiku_c5_systimer_init() != 0) { tiku_c5_fatal("SYSTIMER initialization"); }
    epoch = now();
    seconds_offset = 0;
}

tiku_clock_arch_time_t tiku_clock_arch_time(void) { return tiku_c5_systimer_ticks(); }
unsigned long tiku_clock_arch_seconds(void)
{
    return seconds_offset + (unsigned long)(((now() - epoch) & TIKU_C5_SYSTIMER_MASK) / 16000000u);
}
void tiku_clock_arch_set_seconds(unsigned long seconds)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    epoch = now();
    seconds_offset = seconds;
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_clock_arch_wait(tiku_clock_arch_time_t ticks)
{
    while (ticks--) { tiku_cpu_c5_delay_us(7813); }
}
void tiku_clock_arch_delay(unsigned int us) { tiku_cpu_c5_delay_us(us); }
unsigned short tiku_clock_arch_fine(void) { return (unsigned short)((now() % 125000u) / 2u); }
int tiku_clock_arch_fine_max(void) { return 62500; }
unsigned char tiku_clock_arch_fault(void) { return TIKU_CLOCK_ARCH_FAULT_NONE; }

/** @brief Disarm alarm 1 before calling a callback that may rearm it. */
static void htimer_interrupt(void)
{
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) & ~TIKU_C5_SYSTIMER_TARGET(1));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 2u);
    tiku_htimer_run_next();
}

void tiku_htimer_arch_init(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (!tiku_c5_irq_owned(1, 62, htimer_interrupt) &&
        tiku_c5_irq_attach(1, 62, 2, htimer_interrupt) != 0) {
        tiku_c5_fatal("hardware timer IRQ ownership");
    }
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) & ~TIKU_C5_SYSTIMER_TARGET(1));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_CFG(1), 0);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 2u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_ENA,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_INT_ENA) | 2u);
    (void)tiku_c5_irq_enable(1, 1);
    TIKU_C5_IRQ_RESTORE(state);
}

tiku_htimer_clock_t tiku_htimer_arch_now(void) { return (tiku_htimer_clock_t)(now() / 16u); }
void tiku_htimer_arch_schedule(tiku_htimer_clock_t time)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    uint64_t current = now(), deadline;
    int16_t delta = (int16_t)(time - (tiku_htimer_clock_t)(current / 16u));
    uint32_t conf = TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF);

    deadline = (current + (delta > 4 ? (uint32_t)delta * 16u : 64u)) & TIKU_C5_SYSTIMER_MASK;
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF, conf & ~TIKU_C5_SYSTIMER_TARGET(1));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 2u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_HI(1), (uint32_t)(deadline >> 32));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LO(1), (uint32_t)deadline);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LOAD(1), 1u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF, conf | TIKU_C5_SYSTIMER_TARGET(1));
    TIKU_C5_IRQ_RESTORE(state);
}

void tiku_crit_arch_mask_irqs(uint8_t preserve)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    unsigned line;
    saved_enables = 0;
    for (line = 0; line < 32; line++) {
        uintptr_t address = TIKU_C5_CLIC_CTRL(line + 16u);
        uint32_t value = TIKU_C5_REG_READ(address);
        if (value & 0x100u) { saved_enables |= 1u << line; }
        if ((line == 0 && (preserve & TIKU_CRIT_PRESERVE_TICK)) ||
            (line == 1 && (preserve & TIKU_CRIT_PRESERVE_HTIMER))) { continue; }
        if (value & 0x100u) {
            TIKU_C5_REG_WRITE(address, value & ~0x100u);
        }
    }
    TIKU_C5_IRQ_RESTORE(state);
}

void tiku_crit_arch_unmask_irqs(void)
{
    unsigned line;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    for (line = 0; line < 32; line++) {
        uintptr_t address = TIKU_C5_CLIC_CTRL(line + 16u);
        uint32_t value = TIKU_C5_REG_READ(address) & ~0x100u;
        if (saved_enables & (1u << line)) { value |= 0x100u; }
        TIKU_C5_REG_WRITE(address, value);
    }
    TIKU_C5_IRQ_RESTORE(state);
}

void tiku_wake_arch_query(tiku_wake_sources_t *out)
{
    unsigned i;
    if (out == NULL) { return; }
    out->sources = 0;
    for (i = 0; i < TIKU_WAKE_MAX_GPIO_PORTS; i++) { out->gpio_ie[i] = 0; }
    if (TIKU_C5_REG_READ(TIKU_C5_CLIC_CTRL(16)) & 0x100u) { out->sources |= TIKU_WAKE_SYSTICK; }
    if ((TIKU_C5_REG_READ(TIKU_C5_CLIC_CTRL(17)) & 0x100u) &&
        (TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) & TIKU_C5_SYSTIMER_TARGET(1))) {
        out->sources |= TIKU_WAKE_HTIMER;
    }
    if ((TIKU_C5_REG_READ(TIKU_C5_CLIC_CTRL(TIKU_C5_UART0_IRQ_LINE + 16u)) & 0x100u) &&
        TIKU_C5_REG_READ(TIKU_C5_IRQ_MAP(TIKU_C5_UART0_IRQ_SOURCE)) == TIKU_C5_UART0_IRQ_LINE + 16u &&
        (TIKU_C5_REG_READ(TIKU_C5_UART0_INT_ENABLE) & TIKU_C5_UART0_RX_INTERRUPTS)) {
        out->sources |= TIKU_WAKE_UART_RX;
    }
}
