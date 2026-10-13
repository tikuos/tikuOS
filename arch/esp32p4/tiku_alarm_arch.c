/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_alarm_arch.c - P4 owned one-shot alarm, cancel and callback rearm.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_alarm_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_esp32p4_regs.h"

static void (*alarm_callback)(void);
static uint8_t alarm_armed;

/** @brief Disable comparator 1 and acknowledge its pending status. */
static void alarm_disarm(void)
{
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF,
        TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF) & ~TIKU_P4_SYSTIMER_TARGET(1));
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_CLR, 2u);
    alarm_armed = 0;
}

/** @brief Disarm before delivery so callback rearming remains effective. */
static void alarm_interrupt(void)
{
    int deliver = alarm_armed;
    alarm_disarm();
    if (deliver && alarm_callback != NULL) alarm_callback();
}

/** @brief Test the source and handler, including external detach/replacement. */
static int alarm_owned(void)
{
    return alarm_callback != NULL && tiku_p4_irq_owned(1, 54, alarm_interrupt);
}

int tiku_p4_alarm_init(void (*callback)(void))
{
    uint32_t state;
    if (callback == NULL) return -1;
    state = TIKU_P4_IRQ_SAVE();
    if ((alarm_callback && (alarm_callback != callback || !alarm_owned())) ||
        (!alarm_callback && tiku_p4_irq_attach(1, 54, 2, alarm_interrupt) != 0)) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    alarm_callback = callback;
    alarm_disarm();
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_CFG(1), 0);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_ENA,
                      TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) | 2u);
    (void)tiku_p4_irq_enable(1, 1);
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
}

int tiku_p4_alarm_schedule(uint64_t deadline)
{
    uint64_t now, delta;
    uint32_t state;
    if (deadline > TIKU_P4_SYSTIMER_MASK) return -1;
    state = TIKU_P4_IRQ_SAVE();
    if (!alarm_owned()) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    alarm_disarm();
    if (tiku_p4_systimer_read(&now) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    delta = (deadline - now) & TIKU_P4_SYSTIMER_MASK;
    if (delta < 64u || delta >= (1ULL << 51)) {
        deadline = (now + 64u) & TIKU_P4_SYSTIMER_MASK;
    }
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_HI(1),
                      (uint32_t)(deadline >> 32));
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_LO(1), (uint32_t)deadline);
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_TARGET_LOAD(1), 1u);
    alarm_armed = 1;
    TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_CONF,
        TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_CONF) | TIKU_P4_SYSTIMER_TARGET(1));
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
}

void tiku_p4_alarm_cancel(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (alarm_owned()) alarm_disarm();
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_p4_alarm_stop(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (alarm_owned()) {
        alarm_disarm();
        TIKU_P4_REG_WRITE(TIKU_P4_SYSTIMER_INT_ENA,
                          TIKU_P4_REG_READ(TIKU_P4_SYSTIMER_INT_ENA) & ~2u);
        tiku_p4_irq_detach(1);
    }
    alarm_armed = 0;
    alarm_callback = NULL;
    TIKU_P4_IRQ_RESTORE(state);
}
