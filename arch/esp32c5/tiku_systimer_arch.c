/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_systimer_arch.c - C5 crystal-derived counter and tick alarm.
 * The crystal selection sets the fractional divider: 40 and 48 MHz crystals
 * both produce 16 MHz, as specified by the C5 esp_hw_support systimer port.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"

static uint64_t next_tick;
static volatile uint32_t ticks;
static volatile uint32_t interrupts;
static volatile uint32_t errors;
static uint8_t attached;

__attribute__((weak)) void tiku_c5_tick_notify(void) { }

int tiku_c5_systimer_read(uint64_t *value)
{
    uint32_t state, tries;

    if (value == NULL) {
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_OP, TIKU_C5_SYSTIMER_UPDATE);
    for (tries = 0; tries < 4096u; tries++) {
        if (TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_OP) & TIKU_C5_SYSTIMER_VALID) {
            uint32_t low = TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_VALUE_LO);
            uint32_t high = TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_VALUE_HI);
            *value = ((uint64_t)(high & 0xFFFFFu) << 32) | low;
            TIKU_C5_IRQ_RESTORE(state);
            return 0;
        }
    }
    errors++;
    TIKU_C5_IRQ_RESTORE(state);
    return -1;
}

/** @brief Load a one-shot alarm while the CLIC handler cannot run. */
static void tick_arm(uint64_t deadline)
{
    uint32_t conf = TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      conf & ~TIKU_C5_SYSTIMER_TARGET(0));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 1u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_HI(0),
                      (uint32_t)(deadline >> 32) & 0xFFFFFu);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LO(0), (uint32_t)deadline);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_LOAD(0), 1u);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                      conf | TIKU_C5_SYSTIMER_TARGET(0));
}

/** @brief Count elapsed ticks in constant time and rearm at the next boundary. */
static void tick_interrupt(void)
{
    uint64_t now, elapsed, due;

    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 1u);
    interrupts++;
    if (tiku_c5_systimer_read(&now) != 0) {
        tiku_c5_systimer_stop();
        return;
    }
    elapsed = (now - next_tick) & TIKU_C5_SYSTIMER_MASK;
    if (elapsed < (1ULL << 51)) {
        due = elapsed / TIKU_C5_TICK_COUNTS + 1u;
        ticks += (uint32_t)due;
        next_tick = (next_tick + due * TIKU_C5_TICK_COUNTS) &
                    TIKU_C5_SYSTIMER_MASK;
    }
    tick_arm(next_tick);
    tiku_c5_tick_notify();
}

int tiku_c5_systimer_init(void)
{
    uint32_t state, conf;
    uint64_t now;

    state = TIKU_C5_IRQ_SAVE();
    if (attached && !tiku_c5_irq_owned(0, TIKU_C5_SYSTIMER_IRQ0, tick_interrupt)) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    if (!attached && tiku_c5_irq_attach(0, TIKU_C5_SYSTIMER_IRQ0, 1,
                                       tick_interrupt) != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    attached = 1;
    ticks = interrupts = errors = 0;
    conf = TIKU_C5_REG_READ(TIKU_C5_PCR_SYSTIMER);
    TIKU_C5_REG_WRITE(TIKU_C5_PCR_SYSTIMER, (conf | 1u) & ~2u);
    conf = TIKU_C5_REG_READ(TIKU_C5_PCR_SYSTIMER_CLK);
    TIKU_C5_REG_WRITE(TIKU_C5_PCR_SYSTIMER_CLK,
                      (conf | (1u << 22)) & ~(1u << 20));
    conf = TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF);
    conf &= ~((1u << 28) | TIKU_C5_SYSTIMER_TARGET(0));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF, conf | (3u << 30));
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_TARGET_CFG(0), 0);
    if (tiku_c5_systimer_read(&now) != 0) {
        tiku_c5_systimer_stop();
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    next_tick = (now + TIKU_C5_TICK_COUNTS) & TIKU_C5_SYSTIMER_MASK;
    tick_arm(next_tick);
    TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_ENA,
                      TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_INT_ENA) | 1u);
    (void)tiku_c5_irq_enable(0, 1);
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
}

void tiku_c5_systimer_stop(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();

    if (attached) {
        TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_ENA,
                          TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_INT_ENA) & ~1u);
        TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_CONF,
                          TIKU_C5_REG_READ(TIKU_C5_SYSTIMER_CONF) &
                          ~TIKU_C5_SYSTIMER_TARGET(0));
        TIKU_C5_REG_WRITE(TIKU_C5_SYSTIMER_INT_CLR, 1u);
        tiku_c5_irq_detach(0);
        attached = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
}

uint32_t tiku_c5_systimer_ticks(void)
{
    return ticks;
}

uint32_t tiku_c5_systimer_interrupts(void)
{
    return interrupts;
}

uint32_t tiku_c5_systimer_errors(void)
{
    return errors;
}
