/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_systimer_arch.c - P4 crystal-derived counter and tick alarm.
 *
 * The board's 40 MHz crystal supplies the fixed divide-by-2.5 timebase.
 * Comparator 0 on interrupt line 0 raises the 128 Hz tick; the ISR counts
 * ticks it missed before it rearms.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"

static uint64_t next_tick;
static volatile uint32_t ticks;
static volatile uint32_t interrupts;
static volatile uint32_t errors;
static uint8_t attached;

__attribute__((weak)) void tiku_p4_tick_notify(void)
{
}

int tiku_p4_systimer_read(uint64_t *value)
{
    uint32_t state, tries;

    if (value == NULL) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_OP, TIKU_P4_SYSTIMER_UPDATE);
    for (tries = 0; tries < 4096u; tries++) {
        if (TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_OP) & TIKU_P4_SYSTIMER_VALID) {
            uint32_t low = TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_VALUE_LO);
            uint32_t high = TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_VALUE_HI);
            *value = ((uint64_t)(high & 0xFFFFFu) << 32) | low;
            TIKU_P4_IRQ_RESTORE(state);
            return 0;
        }
    }
    errors++;
    TIKU_P4_IRQ_RESTORE(state);
    return -1;
}

/** @brief Load a one-shot alarm while the CLIC handler cannot run. */
static void tick_arm(uint64_t deadline)
{
    uint32_t conf = TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF,
                      conf & ~TIKU_P4_SYSTIMER_TARGET(0));
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_CLR, 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_HI(0),
                      (uint32_t)(deadline >> 32) & 0xFFFFFu);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_LO(0), (uint32_t)deadline);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_LOAD(0), 1u);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF, conf | TIKU_P4_SYSTIMER_TARGET(0));
}

/** @brief Count elapsed ticks in constant time and rearm at the next boundary.
 */
static void tick_interrupt(void)
{
    uint64_t now, elapsed, due;

    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_CLR, 1u);
    interrupts++;
    if (tiku_p4_systimer_read(&now) != 0) {
        tiku_p4_systimer_stop();
        return;
    }
    elapsed = (now - next_tick) & TIKU_P4_SYSTIMER_MASK;
    if (elapsed < (1ULL << 51)) {
        due = elapsed / TIKU_P4_TICK_COUNTS + 1u;
        ticks += (uint32_t)due;
        next_tick = (next_tick + due * TIKU_P4_TICK_COUNTS) &
                    TIKU_P4_SYSTIMER_MASK;
    }
    tick_arm(next_tick);
    tiku_p4_tick_notify();
}

int tiku_p4_systimer_init(void)
{
    uint32_t state, conf;
    uint64_t now;

    state = TIKU_P4_IRQ_SAVE();
    if (attached &&
        !tiku_p4_irq_owned(0, TIKU_P4_SYSTIMER_IRQ0, tick_interrupt)) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    if (!attached &&
        tiku_p4_irq_attach(0, TIKU_P4_SYSTIMER_IRQ0, 1, tick_interrupt) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    attached = 1;
    ticks = interrupts = errors = 0;
    conf = TIKU_P4_REG_READ(TIKU_P4_SOC_CLK2);
    TIKU_P4_REG_WRITE(TIKU_P4_SOC_CLK2, conf | (1u << 23));
    conf = TIKU_P4_REG_READ(TIKU_P4_HP_RESET1);
    TIKU_P4_REG_WRITE(TIKU_P4_HP_RESET1, conf & ~(1u << 5));
    conf = TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CLK);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CLK,
                      (conf | (1u << 30)) & ~(1u << 29));
    conf = TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF);
    conf &= ~((3u << 27) | TIKU_P4_SYSTIMER_TARGET(0));
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF, conf | (3u << 30));
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_CFG(0), 0);
    if (tiku_p4_systimer_read(&now) != 0) {
        tiku_p4_systimer_stop();
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    next_tick = (now + TIKU_P4_TICK_COUNTS) & TIKU_P4_SYSTIMER_MASK;
    tick_arm(next_tick);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_ENA,
                      TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) | 1u);
    (void)tiku_p4_irq_enable(0, 1);
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
}

void tiku_p4_systimer_stop(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    if (attached &&
        tiku_p4_irq_owned(0, TIKU_P4_SYSTIMER_IRQ0, tick_interrupt)) {
        TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_ENA,
                          TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) & ~1u);
        TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF,
                          TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF) &
                              ~TIKU_P4_SYSTIMER_TARGET(0));
        TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_CLR, 1u);
        tiku_p4_irq_detach(0);
    }
    attached = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

uint32_t tiku_p4_systimer_ticks(void)
{
    return ticks;
}

uint32_t tiku_p4_systimer_interrupts(void)
{
    return interrupts;
}

uint32_t tiku_p4_systimer_errors(void)
{
    return errors;
}
