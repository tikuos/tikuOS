/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_ek_ra8p1.h - EK-RA8P1 v1 board wiring.
 *
 * Pin assignments are from the EK-RA8P1 v1 User's Manual (R20UT5309EG0101);
 * the peripheral each pin belongs to is from the hardware manual's port
 * tables, because the kit manual names pins but not channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_EK_RA8P1_H_
#define TIKU_BOARD_EK_RA8P1_H_

/** @brief Board name for the banner and `info`. */
#define TIKU_BOARD_NAME             "EK-RA8P1 v1"

/*---------------------------------------------------------------------------*/
/* Console                                                                   */
/*                                                                           */
/* Kit UM Table 13 routes the debugger's virtual COM port to PD02/PD03; the   */
/* hardware manual's PORTD table (21.20) gives those pins as TXD8_C/RXD8_C at */
/* PSEL=00100b, so the console is SCI8.                                      */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_CONSOLE_SCI      8U
#define TIKU_BOARD_CONSOLE_TX_PORT  0xDU     /* PORTD */
#define TIKU_BOARD_CONSOLE_TX_PIN   2U       /* PD02 = TXD8_C */
#define TIKU_BOARD_CONSOLE_RX_PORT  0xDU
#define TIKU_BOARD_CONSOLE_RX_PIN   3U       /* PD03 = RXD8_C */

/**
 * @brief Console baud rate.
 *
 * The house 115200, reachable since R4 puts SCICLK on the PLL at 120 MHz.
 * The divisor comes from the live SCICLK, so this constant needs no second
 * value for the 8 MHz boot clock -- it is simply unreachable there.
 */
#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200UL
#endif

/*---------------------------------------------------------------------------*/
/* Oscillators fitted on the board                                           */
/*                                                                           */
/* Kit UM Table 8: E7/E11 tie P212/P213 to a 24 MHz crystal, E55/E56 tie      */
/* P215/P214 to a 32.768 kHz sub-clock crystal.  Neither is running in R2 --  */
/* the part boots on its internal MOCO -- but R4's PLL reference is this      */
/* number, so it belongs to the board, not to the clock driver.               */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_MOSC_HZ          24000000UL
#define TIKU_BOARD_SUBCLK_HZ        32768UL

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*                                                                           */
/* Kit UM Table 24.  Port indices follow the manual's own naming: P600 is     */
/* port 6 pin 0, PA07 is port 0xA pin 7.                                      */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_LED_COUNT        3

#define TIKU_BOARD_LED1_PORT        6U      /* blue,  P600 */
#define TIKU_BOARD_LED1_PIN         0U
#define TIKU_BOARD_LED2_PORT        3U      /* green, P303 */
#define TIKU_BOARD_LED2_PIN         3U
#define TIKU_BOARD_LED3_PORT        0xAU    /* red,   PA07 */
#define TIKU_BOARD_LED3_PIN         7U

/* Active high: the MCU pin drives the anode through the series resistor, so a
 * 1 lights the LED.  Verified by eye on LED1 during R2's smoke test. */
#include <arch/ra8p1/tiku_gpio_arch.h>

#define TIKU_BOARD_LED1_INIT()   tiku_ra8p1_gpio_init_output(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()     tiku_ra8p1_gpio_set(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()    tiku_ra8p1_gpio_set(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE() tiku_ra8p1_gpio_toggle(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN)

#define TIKU_BOARD_LED2_INIT()   tiku_ra8p1_gpio_init_output(TIKU_BOARD_LED2_PORT, TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()     tiku_ra8p1_gpio_set(TIKU_BOARD_LED2_PORT, TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_OFF()    tiku_ra8p1_gpio_set(TIKU_BOARD_LED2_PORT, TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_TOGGLE() tiku_ra8p1_gpio_toggle(TIKU_BOARD_LED2_PORT, TIKU_BOARD_LED2_PIN)

#define TIKU_BOARD_LED3_INIT()   tiku_ra8p1_gpio_init_output(TIKU_BOARD_LED3_PORT, TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()     tiku_ra8p1_gpio_set(TIKU_BOARD_LED3_PORT, TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_OFF()    tiku_ra8p1_gpio_set(TIKU_BOARD_LED3_PORT, TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_TOGGLE() tiku_ra8p1_gpio_toggle(TIKU_BOARD_LED3_PORT, TIKU_BOARD_LED3_PIN)

/*---------------------------------------------------------------------------*/
/* User switches                                                             */
/*                                                                           */
/* Kit UM Table 25.  Both sit on deep-standby-capable IRQ lines, which is     */
/* what makes them the wake source when R8 reaches low power.                 */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_SW1_PORT         0U      /* P009, IRQ13-DS */
#define TIKU_BOARD_SW1_PIN          9U
#define TIKU_BOARD_SW2_PORT         0U      /* P008, IRQ12-DS */
#define TIKU_BOARD_SW2_PIN          8U

#define TIKU_BOARD_BTN1_PORT        TIKU_BOARD_SW1_PORT
#define TIKU_BOARD_BTN1_PIN         TIKU_BOARD_SW1_PIN
#define TIKU_BOARD_BTN1_INIT()      (void)tiku_gpio_arch_set_input(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN)
/* Active low: the switch pulls the pin to ground and the board fits a
 * pull-up, so a read of 0 is a press. */
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_gpio_arch_read(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN) == 0)

#define TIKU_BOARD_BTN2_PORT        TIKU_BOARD_SW2_PORT
#define TIKU_BOARD_BTN2_PIN         TIKU_BOARD_SW2_PIN
#define TIKU_BOARD_BTN2_INIT()      (void)tiku_gpio_arch_set_input(TIKU_BOARD_BTN2_PORT, TIKU_BOARD_BTN2_PIN)
#define TIKU_BOARD_BTN2_PRESSED()   (tiku_gpio_arch_read(TIKU_BOARD_BTN2_PORT, TIKU_BOARD_BTN2_PIN) == 0)

/*---------------------------------------------------------------------------*/
/* Buses with no arch backend yet                                            */
/*                                                                           */
/* The EK routes plenty of these -- SCI, IIC and SPI all reach the Arduino,   */
/* mikroBUS, Qwiic and PMOD headers -- but a board macro describes what the   */
/* SOFTWARE can drive, and none of them has a driver on this port.  Each turns */
/* on with its driver, not before.                                            */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_BSCAT_PORT       0U
#define TIKU_BOARD_BSCAT_PIN        0U
#define TIKU_BOARD_ADC_AVAILABLE    0
#define TIKU_BOARD_I2C_BRW_100K     1   /* symbolic */
#define TIKU_BOARD_OW_AVAILABLE     0
#define TIKU_BOARD_OW_PIN           0U
#define TIKU_BOARD_I2C0_SDA_PIN     0U
#define TIKU_BOARD_I2C0_SCL_PIN     0U
#define TIKU_BOARD_SPI0_MISO_PIN    0U
#define TIKU_BOARD_SPI0_SCK_PIN     0U
#define TIKU_BOARD_SPI0_MOSI_PIN    0U

#endif /* TIKU_BOARD_EK_RA8P1_H_ */
