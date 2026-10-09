/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_analog_arch.c - serialize SAR entropy and the C5 radio front end.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_analog_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"

static unsigned analog_owner;

int tiku_c5_analog_acquire(unsigned owner)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    int result = -1;
    if (owner == TIKU_C5_ANALOG_PHY &&
        ((TIKU_C5_REG_READ(0x6000E000u) & 2u) ||
         (TIKU_C5_REG_READ(0x6000E058u) & (1u << 22)) ||
         (TIKU_C5_REG_READ(0x6000E004u) & (1u << 24)) ||
         (TIKU_C5_REG_READ(0x6000E020u) & (1u << 29)) ||
         (TIKU_C5_REG_READ(0x600B2824u) & 1u))) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    if ((owner == TIKU_C5_ANALOG_ENTROPY || owner == TIKU_C5_ANALOG_PHY) &&
        analog_owner == TIKU_C5_ANALOG_NONE) {
        analog_owner = owner;
        result = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

int tiku_c5_analog_release(unsigned owner)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    int result = -1;
    if (owner != TIKU_C5_ANALOG_NONE && owner == analog_owner) {
        analog_owner = TIKU_C5_ANALOG_NONE;
        result = 0;
    }
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

unsigned tiku_c5_analog_owner(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    unsigned owner = analog_owner;
    TIKU_C5_IRQ_RESTORE(state);
    return owner;
}
