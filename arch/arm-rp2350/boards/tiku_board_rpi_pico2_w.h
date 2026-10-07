/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_rpi_pico2_w.h - Raspberry Pi Pico 2 W board definitions.
 *
 * Pin assignments per the Pico 2 W datasheet.  The board LED is wired to the
 * CYW43439's WL_GPIO0, not to a CPU pin, so the LED macros drive header pins:
 * GP25 and GP15, or GP15 alone in CYW43 builds.  The button macros are stubs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_RPI_PICO2_W_H_
#define TIKU_BOARD_RPI_PICO2_W_H_

#include <arch/arm-rp2350/tiku_rp2350_regs.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name string. */
#define TIKU_BOARD_NAME             "Raspberry Pi Pico 2 W"

/*---------------------------------------------------------------------------*/
/* LED COUNT                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of on-board user LEDs visible to the kernel.
 *
 * With TIKU_DRV_WIFI_CYW43_ENABLE=1, GP25 is the CYW43's WL_CS and only
 * LED1 (GP15) is reported.  Otherwise GP25 and GP15 are both LED pins, so
 * two LEDs are reported.
 */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED_COUNT        1
#else
#define TIKU_BOARD_LED_COUNT        2
#endif

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
 * @brief LED 1 pin assignment and control macros.
 *
 * LED1 is GP15 when TIKU_DRV_WIFI_CYW43_ENABLE is set: GP25 is then WL_CS, and
 * the LED init must not drive it low before the radio samples its reset
 * strap.  Otherwise LED1 is GP25.
 */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#define TIKU_BOARD_LED1_PIN         15U
#else
#define TIKU_BOARD_LED1_PIN         25U
#endif
#define TIKU_BOARD_LED1_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED1_PIN)

/**
 * @brief LED 2 pin assignment and control macros (GP15, non-CYW43 builds).
 *
 * In CYW43 builds GP15 is LED1 and TIKU_BOARD_LED_COUNT is 1, so the LED
 * interface does not use these macros.
 */
#define TIKU_BOARD_LED2_PIN         15U
#define TIKU_BOARD_LED2_INIT()      tiku_rp2350_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_OFF()       tiku_rp2350_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_rp2350_gpio_toggle(TIKU_BOARD_LED2_PIN)

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
 * GP14 (header pin 19) by default, clear of the UART, SPI, I2C, LED and
 * 1-Wire pins, so a scope can watch the test_bitbang pattern there.  The port
 * is 0.  Override the pin with -DTIKU_BOARD_BSCAT_PIN=<n>.
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
 * @brief 1-Wire data pin assignment (GP22).
 *
 * GP22 is clear of the default UART, I2C, SPI, ADC, CYW43 and LED pins.
 * The data line needs an external 4.7 kohm pull-up to 3V3.
 */
#ifndef TIKU_BOARD_OW_PIN
#define TIKU_BOARD_OW_PIN           22U
#endif

/**
 * @brief I2C0 pin assignment (GP4=SDA, GP5=SCL).
 *
 * Function 3 on RP2350 IO_BANK0.  GP4/GP5 are the Pico SDK's default I2C0
 * pair.  Both lines need external pull-ups.
 */
#define TIKU_BOARD_I2C0_SDA_PIN     4U
#define TIKU_BOARD_I2C0_SCL_PIN     5U

/**
 * @brief SPI0 pin assignment (GP16=MISO, GP18=SCK, GP19=MOSI).
 *
 * Function 1 on RP2350 IO_BANK0, the standard Pico/Pico 2 mapping.  The
 * driver does not drive CS: the application drives a free GPIO as CS.
 */
#define TIKU_BOARD_SPI0_MISO_PIN    16U
#define TIKU_BOARD_SPI0_SCK_PIN     18U
#define TIKU_BOARD_SPI0_MOSI_PIN    19U

/*---------------------------------------------------------------------------*/
/* CYW43439 (WIFI/BT MODULE) PINOUT                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief CYW43439 gSPI bus pin assignments, fixed by the Pico 2 W board.
 *
 * The gSPI bus has one bidirectional DATA line, so the driver runs it on PIO:
 * the RP2350 SPI peripheral needs separate MOSI and MISO pins.  The CYW43
 * driver uses these pins when TIKU_DRV_WIFI_CYW43_ENABLE=1.
 *
 * @note GP29 is also ADC channel 3 (the VSYS/3 battery sense).  While the
 *       radio is active GP29 carries the gSPI clock, so that channel does not
 *       read VSYS.
 */
#define TIKU_BOARD_CYW43_WL_REG_ON_PIN  23U  /**< Power-on enable */
#define TIKU_BOARD_CYW43_WL_DATA_PIN    24U  /**< Bidirectional gSPI DATA */
#define TIKU_BOARD_CYW43_WL_CS_PIN      25U  /**< Chip select */
#define TIKU_BOARD_CYW43_WL_CLOCK_PIN   29U  /**< gSPI clock */

#endif /* TIKU_BOARD_RPI_PICO2_W_H_ */
