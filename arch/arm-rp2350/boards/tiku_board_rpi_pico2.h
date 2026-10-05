/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_rpi_pico2.h - Raspberry Pi Pico 2 board definitions.
 *
 * The Pico 2 has no wireless module, and its user LED is wired to GP25.  The
 * UART, I2C, SPI, 1-Wire and bit-bang pins match the Pico 2 W, so code that
 * does not use the CYW43 runs on both boards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_RPI_PICO2_H_
#define TIKU_BOARD_RPI_PICO2_H_

#include <arch/arm-rp2350/tiku_rp2350_regs.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of on-board user LEDs.
 *
 * One user LED, wired to GP25.
 */
#define TIKU_BOARD_LED_COUNT        1

/*---------------------------------------------------------------------------*/
/* GPIO LED HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/* Defined in arch/arm-rp2350/tiku_gpio_arch.c and declared here, so the LED
 * macros below need not include the GPIO header. */

/**
 * @brief Configure an absolute GPIO pin as a SIO push-pull output, driven low.
 *
 * Pins above GP29 are ignored.
 *
 * @param pin  Absolute GP pin number (0..29).
 */
void tiku_rp2350_gpio_init_output(uint8_t pin);

/**
 * @brief Drive an absolute GPIO pin to a logic level.
 *
 * Writes SIO GPIO_OUT_SET or GPIO_OUT_CLR, an atomic update that leaves every
 * other pin alone.  The pin must already be an output; pins above GP29 are
 * ignored.
 *
 * @param pin    Absolute GP pin number (0..29).
 * @param value  0 drives the pin low; non-zero drives it high.
 */
void tiku_rp2350_gpio_set(uint8_t pin, uint8_t value);

/**
 * @brief Toggle an absolute GPIO output pin.
 *
 * Writes the SIO GPIO_OUT_XOR register: one atomic flip that does not
 * disturb other pins.  Pins above GP29 are ignored.
 *
 * @param pin  Absolute GP pin number (0..29).
 */
void tiku_rp2350_gpio_toggle(uint8_t pin);

/**
 * @brief LED 1 pin assignment and control macros (GP25).
 *
 * Drive the pin high to illuminate the LED. These macros are consumed
 * by interfaces/led/tiku_led.c to implement the indexed LED API.
 */
#define TIKU_BOARD_LED1_PIN         25U
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* BACKCHANNEL UART: TX=GP0, RX=GP1 (UART0)                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART0 backchannel pin assignments.
 *
 * Function 2 on GP0 (TX) and GP1 (RX).  tiku_uart_arch.c sets the IO_BANK0
 * and PADS_BANK0 mux for these pins itself and does not read these macros,
 * so TIKU_BOARD_UART_PINS_INIT() is empty.
 */
#define TIKU_BOARD_UART_TX_PIN      0U
#define TIKU_BOARD_UART_RX_PIN      1U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Button macros: INIT does nothing and PRESSED is always 0.
 *
 * The only button is BOOTSEL, which sits on the QSPI bank, not bank 0.
 * Reading it needs XIP disabled for the duration of the read.
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* BIT-BANG AND BACKSCATTER PIN                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter output pin assignment.
 *
 * GP14 by default, clear of the UART, I2C, SPI, LED and 1-Wire pins.  The
 * RP2350 has one GPIO bank, so the port is 0.  Override the pin with
 * -DTIKU_BOARD_BSCAT_PIN=<n>.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        14U
#endif

/*---------------------------------------------------------------------------*/
/* BUS AVAILABILITY                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bus and peripheral availability flags.
 *
 * The platform-independent ADC, I2C and 1-Wire layers compile to empty
 * translation units unless these are defined.  The value of I2C_BRW_100K is
 * unused here; tiku_i2c_arch.c sets the bus speed.
 */
#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1   /* presence flag; value unused */
#define TIKU_BOARD_OW_AVAILABLE     1

/**
 * @brief 1-Wire data pin assignment.
 *
 * GP15 has no other use on this board.  The data line needs an external
 * 4.7 kohm pull-up to 3V3.
 */
#define TIKU_BOARD_OW_PIN           15U

/**
 * @brief I2C0 pin assignment (GP4=SDA, GP5=SCL).
 *
 * Standard Pico mapping; function 3 on RP2350 IO_BANK0. External
 * pull-ups required on both lines.
 */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/**
 * @brief SPI0 pin assignment (GP16=MISO, GP18=SCK, GP19=MOSI).
 *
 * Standard Pico/Pico 2 mapping; function 1 on RP2350 IO_BANK0.
 * CS (SS) is left to the application — drive any free GPIO from
 * user code.
 */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/* This board has no CYW43 module and defines no TIKU_BOARD_CYW43_* pins.  The
 * Makefile refuses TIKU_DRV_WIFI_CYW43_ENABLE=1 unless BOARD=pico2w. */

#endif /* TIKU_BOARD_RPI_PICO2_H_ */
