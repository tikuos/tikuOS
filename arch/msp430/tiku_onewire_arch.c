/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire_arch.c - 1-Wire bus driver for MSP430 (GPIO bit-bang).
 *
 * Bit-bangs the Dallas/Maxim protocol on the board's TIKU_BOARD_OW_* pin, timed
 * from an 8 MHz MCLK.  The line needs an external 4.7 kohm pull-up, and interrupts
 * are masked across timing-critical windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_onewire_arch.h"
#include "tiku.h"

#ifdef TIKU_BOARD_OW_AVAILABLE  /* Board supports 1-Wire */

/*---------------------------------------------------------------------------*/
/* TIMING MACROS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * Microsecond delay assuming 8 MHz MCLK.
 * __delay_cycles() is available in both TI and GCC MSP430 toolchains.
 */
#define OW_DELAY_US(us)  __delay_cycles((unsigned long)(us) * 8UL)

/*---------------------------------------------------------------------------*/
/* GPIO HELPERS                                                              */
/*---------------------------------------------------------------------------*/

/** Drive the 1-Wire line low (output, low). */
static inline void
ow_drive_low(void)
{
    TIKU_BOARD_OW_OUT &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_DIR |= TIKU_BOARD_OW_BIT;
}

/** Release the 1-Wire line (input, external pull-up drives high). */
static inline void
ow_release(void)
{
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
}

/** Read the current state of the 1-Wire line. */
static inline uint8_t
ow_read(void)
{
    return (TIKU_BOARD_OW_IN & TIKU_BOARD_OW_BIT) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the 1-Wire GPIO pin.
 *
 * Configures the pin as input (pull-up drives the line high),
 * clears the output latch, and ensures GPIO mode (SEL0=SEL1=0).
 */
int
tiku_onewire_arch_init(void)
{
    /* Start with line released (input, pull-up drives high) */
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_OUT &= ~TIKU_BOARD_OW_BIT;

    /* Ensure pin is in GPIO mode (SEL0=SEL1=0) */
    TIKU_BOARD_OW_SEL0 &= ~TIKU_BOARD_OW_BIT;
    TIKU_BOARD_OW_SEL1 &= ~TIKU_BOARD_OW_BIT;

    return TIKU_OW_OK;
}

/** @brief Release the 1-Wire pin (set to input, external pull-up holds high). */
void
tiku_onewire_arch_close(void)
{
    /* Release the line */
    TIKU_BOARD_OW_DIR &= ~TIKU_BOARD_OW_BIT;
}

/**
 * @brief 1-Wire reset: 480 us low pulse, then listen for presence.
 *
 * Drives low 480 us and releases, first checking the line returns HIGH so the
 * pull-up is proven working, then sampling around the 70 us mark for a slave
 * pulling it LOW.  The remaining 410 us completes the reset window.
 */
int
tiku_onewire_arch_reset(void)
{
    uint8_t line_high;
    uint8_t presence;
    uint16_t sr;

    /* Save and disable interrupts for timing-critical section */
    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    ow_drive_low();
    OW_DELAY_US(480);

    ow_release();
    OW_DELAY_US(15);

    /* Verify line went HIGH after release (pull-up is working) */
    line_high = ow_read();

    OW_DELAY_US(55);   /* Wait until ~70 us from release */

    /* Sample: low = device present */
    presence = ow_read();

    OW_DELAY_US(410);

    /* Restore interrupts */
    __bis_SR_register(sr);

    /* If line never went high, pull-up is missing or bus is stuck */
    if (!line_high) {
        return TIKU_OW_ERR_NO_DEVICE;
    }

    return presence ? TIKU_OW_ERR_NO_DEVICE : TIKU_OW_OK;
}

/**
 * @brief Write a single bit.
 *
 * A write-1 drives low 2 us then releases for 62; a write-0 drives low 60 then
 * releases for 4.  The slave samples around 30 us in, so the short write-1
 * pulse guarantees the line is high by then.
 */
void
tiku_onewire_arch_write_bit(uint8_t bit)
{
    uint16_t sr;

    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    if (bit & 1) {
        ow_drive_low();
        OW_DELAY_US(2);
        ow_release();
        OW_DELAY_US(62);
    } else {
        ow_drive_low();
        OW_DELAY_US(60);
        ow_release();
        OW_DELAY_US(4);
    }

    __bis_SR_register(sr);
}

/**
 * @brief Read a single bit.
 *
 * Drive low 2 us, release, wait 10, sample, idle 50 -- the master must sample
 * within 15 us of the falling edge.  The 10 us also covers the input
 * synchroniser, which can lag a direction change by two MCLK cycles.
 */
uint8_t
tiku_onewire_arch_read_bit(void)
{
    uint8_t bit;
    uint16_t sr;

    sr = __get_SR_register() & GIE;
    __bic_SR_register(GIE);

    ow_drive_low();
    OW_DELAY_US(2);

    ow_release();
    OW_DELAY_US(10);

    /* Read PxIN directly as volatile to prevent any caching */
    bit = (*(volatile uint8_t *)&TIKU_BOARD_OW_IN & TIKU_BOARD_OW_BIT) ? 1 : 0;

    OW_DELAY_US(50);

    __bis_SR_register(sr);

    return bit;
}

/**
 * @brief Write a byte (LSB first).
 */
void
tiku_onewire_arch_write_byte(uint8_t byte)
{
    uint8_t i;

    for (i = 0; i < 8; i++) {
        tiku_onewire_arch_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/**
 * @brief Read a byte (LSB first).
 */
uint8_t
tiku_onewire_arch_read_byte(void)
{
    uint8_t byte = 0;
    uint8_t i;

    for (i = 0; i < 8; i++) {
        byte |= (tiku_onewire_arch_read_bit() << i);
    }

    return byte;
}

#endif /* TIKU_BOARD_OW_AVAILABLE */
