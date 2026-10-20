/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - P4 1-Wire master, SYSTIMER-timed slots, pin restore.
 *
 * Reset, presence and slot timings are the standard-speed values the C5
 * driver uses; GPIO fields follow ESP-IDF 4d59230 gpio_reg.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include "tiku_onewire_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"

#define PIN             TIKU_BOARD_OW_PIN
#define BANK            (PIN < 32u ? 0u : 12u)
#define GPIO_OUT        (0x500E0004u + BANK)
#define GPIO_OUT_SET    (0x500E0008u + BANK)
#define GPIO_OUT_CLR    (0x500E000Cu + BANK)
#define GPIO_ENABLE     (PIN < 32u ? 0x500E0020u : 0x500E002Cu)
#define GPIO_INPUT      (PIN < 32u ? 0x500E003Cu : 0x500E0040u)
#define PIN_MUX         (0x500E1004u + 4u * PIN)
#define PIN_CONFIG      (0x500E0074u + 4u * PIN)
#define PIN_ROUTE       (0x500E0558u + 4u * PIN)
#define OW_MASK         (1u << (PIN % 32u))
#define TICKS_PER_US    16u

static uint32_t saved_mux, saved_config, saved_route, saved_out, saved_enable;
static uint8_t opened;

/** @brief SYSTIMER count now, or 0 if the timer does not answer. */
static uint64_t ticks(void)
{
    uint64_t now = 0;
    (void)tiku_p4_systimer_read(&now);
    return now;
}

/** @brief Spin until @p us microseconds after @p start, or until the timer
 *         stops answering. */
static void until(uint64_t start, uint32_t us)
{
    uint64_t now;
    while (tiku_p4_systimer_read(&now) == 0 &&
           ((now - start) & TIKU_P4_SYSTIMER_MASK) < (uint64_t)us * TICKS_PER_US) {
    }
}

/** @brief Pull the bus low. */
static void bus_low(void)
{
    TIKU_P4_REG_WRITE(GPIO_OUT_CLR, OW_MASK);
}

/** @brief Release the bus to its pull-up. */
static void bus_release(void)
{
    TIKU_P4_REG_WRITE(GPIO_OUT_SET, OW_MASK);
}

/** @brief The level on the bus. */
static uint8_t bus_level(void)
{
    return (uint8_t)((TIKU_P4_REG_READ(GPIO_INPUT) >> (PIN % 32u)) & 1u);
}

int tiku_onewire_arch_init(void)
{
    uint32_t state;

    if (PIN > 54u) {
        return TIKU_OW_ERR_PARAM;
    }
    if (opened) {
        return TIKU_OW_OK;
    }
    state = TIKU_P4_IRQ_SAVE();
    saved_mux = TIKU_P4_REG_READ(PIN_MUX);
    saved_config = TIKU_P4_REG_READ(PIN_CONFIG);
    saved_route = TIKU_P4_REG_READ(PIN_ROUTE);
    saved_out = TIKU_P4_REG_READ(GPIO_OUT) & OW_MASK;
    saved_enable = TIKU_P4_REG_READ(GPIO_ENABLE) & OW_MASK;
    /* Released before the driver turns on: an open-drain 1 is high impedance. */
    bus_release();
    /* GPIO function, input on, pull-up on, pull-down off. */
    TIKU_P4_REG_WRITE(PIN_MUX, (saved_mux & ~((7u << 12) | (3u << 7))) |
                                   (1u << 12) | (1u << 9) | (1u << 8));
    TIKU_P4_REG_WRITE(PIN_CONFIG, saved_config | (1u << 2));
    TIKU_P4_REG_WRITE(PIN_ROUTE, 256u);
    TIKU_P4_REG_WRITE(GPIO_ENABLE + 4u, OW_MASK);
    opened = 1;
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_OW_OK;
}

void tiku_onewire_arch_close(void)
{
    uint32_t state;

    if (!opened) {
        return;
    }
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(GPIO_ENABLE + 8u, OW_MASK);
    TIKU_P4_REG_WRITE(PIN_ROUTE, saved_route);
    TIKU_P4_REG_WRITE(PIN_CONFIG, saved_config);
    TIKU_P4_REG_WRITE(PIN_MUX, saved_mux);
    TIKU_P4_REG_WRITE(saved_out ? GPIO_OUT_SET : GPIO_OUT_CLR, OW_MASK);
    if (saved_enable) {
        TIKU_P4_REG_WRITE(GPIO_ENABLE + 4u, OW_MASK);
    }
    opened = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

int tiku_onewire_arch_reset(void)
{
    uint32_t state;
    uint64_t start;
    uint8_t present;

    if (!opened) {
        return TIKU_OW_ERR_NO_DEVICE;
    }
    /* An interrupt may lengthen the low pulse; the presence sample is timed
     * from the release with interrupts masked. */
    start = ticks();
    bus_low();
    until(start, 480u);
    state = TIKU_P4_IRQ_SAVE();
    start = ticks();
    bus_release();
    until(start, 70u);
    present = (uint8_t)!bus_level();
    TIKU_P4_IRQ_RESTORE(state);
    until(start, 480u);
    return present ? TIKU_OW_OK : TIKU_OW_ERR_NO_DEVICE;
}

void tiku_onewire_arch_write_bit(uint8_t bit)
{
    uint32_t state;
    uint64_t start;

    if (!opened) {
        return;
    }
    state = TIKU_P4_IRQ_SAVE();
    start = ticks();
    bus_low();
    until(start, bit ? 6u : 60u);
    bus_release();
    until(start, 70u);
    TIKU_P4_IRQ_RESTORE(state);
}

uint8_t tiku_onewire_arch_read_bit(void)
{
    uint32_t state;
    uint64_t start;
    uint8_t level;

    if (!opened) {
        return 1u;
    }
    state = TIKU_P4_IRQ_SAVE();
    start = ticks();
    bus_low();
    until(start, 6u);
    bus_release();
    until(start, 15u);
    level = bus_level();
    until(start, 70u);
    TIKU_P4_IRQ_RESTORE(state);
    return level;
}

void tiku_onewire_arch_write_byte(uint8_t byte)
{
    unsigned i;

    for (i = 0; i < 8u; i++) {
        tiku_onewire_arch_write_bit((uint8_t)((byte >> i) & 1u));
    }
}

uint8_t tiku_onewire_arch_read_byte(void)
{
    uint8_t byte = 0;
    unsigned i;

    for (i = 0; i < 8u; i++) {
        byte |= (uint8_t)(tiku_onewire_arch_read_bit() << i);
    }
    return byte;
}
