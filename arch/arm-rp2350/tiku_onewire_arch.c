/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - 1-Wire bus driver for RP2350 (GPIO bit-bang).
 *
 * Bit-bangs the Dallas/Maxim protocol through SIO.  The driver releases the
 * line by turning the pin's output off, and an external pull-up holds it high.
 * Slot timing spins on the 1 us TIMER0 counter, independent of clk_sys.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_common.h"
#include <hal/tiku_cpu.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* PIN SELECTION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief 1-Wire GPIO number (0-29); GP15 unless the board header sets it.
 */
#ifndef TIKU_BOARD_OW_PIN
#define TIKU_BOARD_OW_PIN  15U
#endif

/** @brief The 1-Wire pin's bit in the SIO GPIO registers. */
#define OW_PIN_MASK        (1U << TIKU_BOARD_OW_PIN)

/*---------------------------------------------------------------------------*/
/* GPIO HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Drive the 1-Wire line low (clear OUT, then assert OE). */
static inline void ow_drive_low(void) {
    _RP2350_REG(RP2350_SIO_GPIO_OUT_CLR) = OW_PIN_MASK;
    _RP2350_REG(RP2350_SIO_GPIO_OE_SET)  = OW_PIN_MASK;
}

/** @brief Release the 1-Wire line to high-impedance (clear OE).
 *
 *  The external 4.7 kohm pull-up restores the bus to logic-high.
 */
static inline void ow_release(void) {
    _RP2350_REG(RP2350_SIO_GPIO_OE_CLR) = OW_PIN_MASK;
}

/** @brief Sample the 1-Wire line level.
 *
 *  @return 1 if the bus is high, 0 if low.
 */
static inline uint8_t ow_read(void) {
    return (_RP2350_REG(RP2350_SIO_GPIO_IN) & OW_PIN_MASK) ? 1U : 0U;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_onewire_arch_init(void) {
    /* Pad: input buffer on, so SIO GPIO_IN reads the pin, and no pulls.
     * IO_BANK0: function SIO. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_OW_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_OW_PIN)) =
        RP2350_IO_FUNC_SIO;

    /* Start released (high-Z). External pull-up holds the bus idle. */
    ow_release();
    return TIKU_OW_OK;
}

void tiku_onewire_arch_close(void) {
    /* Float the pin, then disable the pad's output driver and input
     * buffer. */
    ow_release();
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_OW_PIN)) =
        RP2350_PADS_OD;
}

int tiku_onewire_arch_reset(void) {
    uint8_t presence;

    tiku_cpu_irq_disable();

    ow_drive_low();
    tiku_cpu_rp2350_delay_us(480U);

    ow_release();
    /* Wait into the device's response window, then sample. */
    tiku_cpu_rp2350_delay_us(70U);
    presence = ow_read();

    /* Finish the 480 us recovery window so the bus is idle on
     * return. */
    tiku_cpu_rp2350_delay_us(410U);

    tiku_cpu_irq_enable();

    /* Device pulls the line low to indicate presence. */
    return (presence == 0U) ? TIKU_OW_OK : TIKU_OW_ERR_NO_DEVICE;
}

void tiku_onewire_arch_write_bit(uint8_t bit) {
    tiku_cpu_irq_disable();
    if (bit & 0x1U) {
        ow_drive_low();
        tiku_cpu_rp2350_delay_us(6U);
        ow_release();
        tiku_cpu_rp2350_delay_us(64U);
    } else {
        ow_drive_low();
        tiku_cpu_rp2350_delay_us(60U);
        ow_release();
        tiku_cpu_rp2350_delay_us(10U);
    }
    tiku_cpu_irq_enable();
}

uint8_t tiku_onewire_arch_read_bit(void) {
    uint8_t bit;

    tiku_cpu_irq_disable();
    ow_drive_low();
    tiku_cpu_rp2350_delay_us(6U);
    ow_release();
    tiku_cpu_rp2350_delay_us(9U);
    bit = ow_read();
    tiku_cpu_rp2350_delay_us(55U);
    tiku_cpu_irq_enable();

    return bit;
}

void tiku_onewire_arch_write_byte(uint8_t byte) {
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        tiku_onewire_arch_write_bit(byte & 0x1U);
        byte >>= 1U;
    }
}

uint8_t tiku_onewire_arch_read_byte(void) {
    uint8_t byte = 0U;
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        byte |= (uint8_t)(tiku_onewire_arch_read_bit() << i);
    }
    return byte;
}
