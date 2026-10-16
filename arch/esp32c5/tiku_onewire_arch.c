/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_onewire_arch.c - C5 1-Wire master: open-drain GPIO, slots timed in
 * CPU cycles, interrupts masked where a late edge would corrupt a slot.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_onewire_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"

#define GPIO_OUT        0x60091004u
#define GPIO_ENABLE     0x60091034u
#define GPIO_INPUT      0x60091064u
#define PIN_MUX(n)      (0x60090000u + 4u * (n))
#define PIN_CONFIG(n)   (0x600910D4u + 4u * (n))
#define PIN_ROUTE(n)    (0x60091AD4u + 4u * (n))
#define OW_MASK         (1u << TIKU_BOARD_OW_PIN)

extern unsigned int tiku_c5_rom_cpu_frequency(void);

static uint32_t saved_mux, saved_config, saved_route, saved_out, saved_enable;
static uint8_t opened;

/** @brief Read the low 32 bits of the CPU cycle counter. */
static uint32_t cycles(void)
{
    uint32_t value;
    __asm__ volatile("csrr %0, mcycle" : "=r"(value));
    return value;
}

/** @brief Spin until @p us microseconds after @p start. */
static void until(uint32_t start, uint32_t us)
{
    uint32_t span = us * tiku_c5_rom_cpu_frequency();
    while ((uint32_t)(cycles() - start) < span) {
    }
}

/** @brief Pull the bus low. */
static void bus_low(void)
{
    TIKU_C5_REG_WRITE(GPIO_OUT + 8u, OW_MASK);
}

/** @brief Release the bus to its pull-up. */
static void bus_release(void)
{
    TIKU_C5_REG_WRITE(GPIO_OUT + 4u, OW_MASK);
}

/** @brief The level on the bus. */
static uint8_t bus_level(void)
{
    return (uint8_t)((TIKU_C5_REG_READ(GPIO_INPUT) >> TIKU_BOARD_OW_PIN) & 1u);
}

int tiku_onewire_arch_init(void)
{
    uint32_t state;

    if (TIKU_BOARD_OW_PIN > 28u) {
        return TIKU_OW_ERR_PARAM;
    }
    if (opened) {
        return TIKU_OW_OK;
    }
    state = TIKU_C5_IRQ_SAVE();
    saved_mux = TIKU_C5_REG_READ(PIN_MUX(TIKU_BOARD_OW_PIN));
    saved_config = TIKU_C5_REG_READ(PIN_CONFIG(TIKU_BOARD_OW_PIN));
    saved_route = TIKU_C5_REG_READ(PIN_ROUTE(TIKU_BOARD_OW_PIN));
    saved_out = TIKU_C5_REG_READ(GPIO_OUT) & OW_MASK;
    saved_enable = TIKU_C5_REG_READ(GPIO_ENABLE) & OW_MASK;
    /* Released before the driver turns on: an open-drain 1 is high impedance. */
    bus_release();
    /* GPIO function, input on, pull-up on, pull-down off. */
    TIKU_C5_REG_WRITE(PIN_MUX(TIKU_BOARD_OW_PIN),
                      (saved_mux & ~((7u << 12) | (3u << 7))) | (1u << 12) |
                          (1u << 9) | (1u << 8));
    TIKU_C5_REG_WRITE(PIN_CONFIG(TIKU_BOARD_OW_PIN), saved_config | (1u << 2));
    TIKU_C5_REG_WRITE(PIN_ROUTE(TIKU_BOARD_OW_PIN), 256u);
    TIKU_C5_REG_WRITE(GPIO_ENABLE + 4u, OW_MASK);
    opened = 1;
    TIKU_C5_IRQ_RESTORE(state);
    return TIKU_OW_OK;
}

void tiku_onewire_arch_close(void)
{
    uint32_t state;

    if (!opened) {
        return;
    }
    state = TIKU_C5_IRQ_SAVE();
    TIKU_C5_REG_WRITE(GPIO_ENABLE + 8u, OW_MASK);
    TIKU_C5_REG_WRITE(PIN_ROUTE(TIKU_BOARD_OW_PIN), saved_route);
    TIKU_C5_REG_WRITE(PIN_CONFIG(TIKU_BOARD_OW_PIN), saved_config);
    TIKU_C5_REG_WRITE(PIN_MUX(TIKU_BOARD_OW_PIN), saved_mux);
    TIKU_C5_REG_WRITE(saved_out ? GPIO_OUT + 4u : GPIO_OUT + 8u, OW_MASK);
    TIKU_C5_REG_WRITE(saved_enable ? GPIO_ENABLE + 4u : GPIO_ENABLE + 8u,
                      OW_MASK);
    opened = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

int tiku_onewire_arch_reset(void)
{
    uint32_t state, start;
    uint8_t present;

    if (!opened) {
        return TIKU_OW_ERR_NO_DEVICE;
    }
    /* An interrupt may lengthen the low pulse; the presence sample is timed
     * from the release with interrupts masked. */
    start = cycles();
    bus_low();
    until(start, 480u);
    state = TIKU_C5_IRQ_SAVE();
    start = cycles();
    bus_release();
    until(start, 70u);
    present = (uint8_t)!bus_level();
    TIKU_C5_IRQ_RESTORE(state);
    until(start, 480u);
    return present ? TIKU_OW_OK : TIKU_OW_ERR_NO_DEVICE;
}

void tiku_onewire_arch_write_bit(uint8_t bit)
{
    uint32_t state, start;

    if (!opened) {
        return;
    }
    state = TIKU_C5_IRQ_SAVE();
    start = cycles();
    bus_low();
    until(start, bit ? 6u : 60u);
    bus_release();
    until(start, 70u);
    TIKU_C5_IRQ_RESTORE(state);
}

uint8_t tiku_onewire_arch_read_bit(void)
{
    uint32_t state, start;
    uint8_t level;

    if (!opened) {
        return 1u;
    }
    state = TIKU_C5_IRQ_SAVE();
    start = cycles();
    bus_low();
    until(start, 6u);
    bus_release();
    until(start, 15u);
    level = bus_level();
    until(start, 70u);
    TIKU_C5_IRQ_RESTORE(state);
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
