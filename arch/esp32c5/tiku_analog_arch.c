/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_analog_arch.c - serialize the C5 SAR's users (entropy, the ADC and the
 * radio front end) and reach the SAR's internal analog registers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_analog_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"

#define C5_ANA          0x600AF800u
#define C5_ANA_SELECT   (C5_ANA + 0x1Cu)
#define C5_ANA_MUX      (C5_ANA + 0x20u)
#define C5_ANA_SPINS    200000u

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
    if ((owner == TIKU_C5_ANALOG_ENTROPY || owner == TIKU_C5_ANALOG_PHY ||
         owner == TIKU_C5_ANALOG_ADC) &&
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

/** @brief Wait a bounded number of reads for the analog bus to become idle. */
static int analog_idle(uint32_t address)
{
    unsigned n;
    for (n = 0; n < C5_ANA_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(address) & (1u << 25))) {
            return 0;
        }
    }
    return -1;
}

int tiku_c5_analog_sar_reg(uint8_t reg, uint8_t *value, int write)
{
    uint32_t address = C5_ANA +
                       ((TIKU_C5_REG_READ(C5_ANA_MUX) & (1u << 11)) ? 0u : 4u);
    uint32_t command = 0x69u | ((uint32_t)reg << 8);
    if (analog_idle(address)) {
        return -1;
    }
    TIKU_C5_REG_WRITE(C5_ANA_SELECT, 0xFFFFFFu & ~(1u << 9));
    if (write) {
        command |= (1u << 24) | ((uint32_t)*value << 16);
    }
    TIKU_C5_REG_WRITE(address, command);
    if (analog_idle(address)) {
        return -1;
    }
    if (!write) {
        *value = (uint8_t)(TIKU_C5_REG_READ(address) >> 16);
    }
    return 0;
}
