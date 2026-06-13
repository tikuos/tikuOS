/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - 1-Wire bus driver for RP2350 (GPIO bit-bang)
 *
 * Implements the Dallas/Maxim 1-Wire protocol on top of the SIO-direct
 * GPIO path. Pin chosen at compile time via TIKU_BOARD_OW_PIN in the
 * board header (default GP15 on Pico 2 W). External 4.7 kohm pull-up
 * to 3V3 is required on the data line — the driver releases the line
 * by floating it as a high-impedance input and lets the pull-up bring
 * it high.
 *
 * Timing: tiku_cpu_rp2350_delay_us() spins on the TIMER0 microsecond
 * counter, so accuracy is +/- 1 us regardless of CPU clock. ARM IRQs
 * are masked across each timing-critical bit slot to keep an unrelated
 * interrupt from stretching the slot past the 1-Wire spec window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_onewire_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_common.h"
#include <hal/tiku_cpu.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Pin selection                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Pin selection macros for the 1-Wire GPIO line.
 *
 *  TIKU_BOARD_OW_PIN is the SIO pin index (0-29).  Override it in the
 *  board header; the default of GP15 matches the Pico 2 W reference
 *  layout.  OW_PIN_MASK is the corresponding single-bit SIO bitmask.
 */
#ifndef TIKU_BOARD_OW_PIN
#define TIKU_BOARD_OW_PIN  15U   /* sane default if no board override */
#endif

#define OW_PIN_MASK        (1U << TIKU_BOARD_OW_PIN)

/*---------------------------------------------------------------------------*/
/* GPIO helpers                                                              */
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
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Configure the GPIO pin for 1-Wire operation and release the bus.
 *
 *  Sets the pad to SIO function with input-enable on and no internal
 *  pulls (the external 4.7 kohm pull-up provides the idle-high level).
 *  Leaves the pin released (high-impedance) on return.
 *
 * @return TIKU_OW_OK always.
 */
int tiku_onewire_arch_init(void) {
    /* Pad config: function = SIO, input enable on (so OW_GPIO_IN reads
     * the actual pin level), no pulls (external 4.7k provides the rail).
     * Drive strength irrelevant for an open-drain bus. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_OW_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_OW_PIN)) =
        RP2350_IO_FUNC_SIO;

    /* Start released (high-Z). External pull-up holds the bus idle. */
    ow_release();
    return TIKU_OW_OK;
}

/**
 * @brief Release the 1-Wire pin and disable its input buffer.
 *
 *  Floats the pin and clears the pad input-enable bit to eliminate the
 *  few microamps drawn by the analogue input stage when the bus is idle.
 */
void tiku_onewire_arch_close(void) {
    /* Float the pin and turn the pad input buffer back off to save
     * the few uA the analog input draws. */
    ow_release();
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_OW_PIN)) =
        RP2350_PADS_OD;
}

/**
 * @brief Issue a 1-Wire reset pulse and detect a presence response.
 *
 *  Master pulls the bus low for 480 us then releases.  The external
 *  pull-up restores the line in <15 us; any attached device pulls it low
 *  for 60-240 us within that recovery window.  The full reset cycle
 *  occupies 480 us low + 480 us recovery.  IRQs are masked throughout
 *  to preserve timing accuracy.
 *
 * @return TIKU_OW_OK if a device presence pulse was detected,
 *         TIKU_OW_ERR_NO_DEVICE if the bus stayed high.
 */
int tiku_onewire_arch_reset(void) {
    uint8_t presence;

    tiku_cpu_irq_disable();

    ow_drive_low();
    tiku_cpu_rp2350_delay_us(480U);

    ow_release();
    /* Wait into the device's response window, then sample. */
    tiku_cpu_rp2350_delay_us(70U);
    presence = ow_read();

    /* Finish the 480 us recovery window so the bus is idle when we
     * return. */
    tiku_cpu_rp2350_delay_us(410U);

    tiku_cpu_irq_enable();

    /* Device pulls the line low to indicate presence. */
    return (presence == 0U) ? TIKU_OW_OK : TIKU_OW_ERR_NO_DEVICE;
}

/**
 * @brief Write one bit onto the 1-Wire bus.
 *
 *  Write-1: pull low 6 us, release, idle 64 us.
 *  Write-0: pull low 60 us, release, idle 10 us.
 *  Total slot is >= 70 us in either case.  IRQs are masked across the
 *  slot to prevent timing violations.
 *
 * @param bit  Value to write; only the LSB is used (0 or non-zero).
 */
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

/**
 * @brief Read one bit from the 1-Wire bus.
 *
 *  Initiates a read slot: pull low 6 us, release, wait 9 us, sample the
 *  line, then pad the slot to 70 us total.  IRQs are masked across the
 *  slot.
 *
 * @return The sampled bit value: 1 if the bus was high, 0 if low.
 */
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

/**
 * @brief Write one byte onto the 1-Wire bus, LSB first.
 *
 * @param byte  Byte value to transmit.
 */
void tiku_onewire_arch_write_byte(uint8_t byte) {
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        tiku_onewire_arch_write_bit(byte & 0x1U);
        byte >>= 1U;
    }
}

/**
 * @brief Read one byte from the 1-Wire bus, LSB first.
 *
 * @return The received byte, assembled from eight consecutive read slots.
 */
uint8_t tiku_onewire_arch_read_byte(void) {
    uint8_t byte = 0U;
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        byte |= (uint8_t)(tiku_onewire_arch_read_bit() << i);
    }
    return byte;
}
