/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_tiku_bare.h - an Apollo510 board with a console and nothing else.
 *
 * The minimum a board header declares: a console UART, no LEDs and no
 * buttons.  BOARD_CAPS_tiku_bare is empty, so the Makefile refuses a build
 * that enables the eMMC, PSRAM, NOR or USB driver for this board.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_TIKU_BARE_H_
#define TIKU_BOARD_TIKU_BARE_H_

#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name. */
#define TIKU_BOARD_NAME             "TikuOS bare (Apollo510)"

/*---------------------------------------------------------------------------*/
/* LEDS                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief No LEDs: tiku_led_count() returns 0 and every LED call does nothing.
 *
 * The LED interface bounds its dispatch by this count, so no TIKU_BOARD_LEDn
 * macro is needed.
 */
#define TIKU_BOARD_LED_COUNT        0

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/*
 * The console is UART0 on pads 30/55 with FUNCSEL 4, as on the Apollo510
 * EVB.  These three values are the only board wiring this header declares.
 */
#define TIKU_BOARD_UART_TX_PIN      30U     /**< UART0 TX pad. */
#define TIKU_BOARD_UART_RX_PIN      55U     /**< UART0 RX pad. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 4U      /**< FUNCSEL for pads 30/55. */
/** @brief Empty: the UART driver muxes the console pads itself. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @name Buttons
 * The board has none: INIT does nothing and PRESSED returns 0.
 * @{
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)
/** @} */

/*---------------------------------------------------------------------------*/
/* BIT-BANG PIN                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default bit-bang pad for the tiku_bitbang demo: port 2 pin 5.
 *
 * The (port, pin) GPIO API maps to pad (port - 1) * 8 + pin, so this is pad
 * 13, clear of the console pads.  Override either value from the build.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       2U   /**< Port 2 -> pad base 8. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        5U   /**< pin 5 -> pad 13. */
#endif

/*---------------------------------------------------------------------------*/
/* BUS GATES                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * The portable ADC and I2C layers build when their gate macro is defined,
 * whatever its value.  This port's ADC driver works; its I2C and 1-Wire
 * drivers touch no hardware, and their init calls return -1.  No Ambiq code
 * reads the 1-Wire, I2C0 or SPI0 pad macros.
 */
/** @brief ADC driver is available. */
#define TIKU_BOARD_ADC_AVAILABLE    1
/** @brief I2C gate: defined, so the I2C bus API builds on the stub driver. */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief No 1-Wire bus: /dev/sensors carries no ds18b20 node. */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire pad (unused). */
#define TIKU_BOARD_OW_PIN           13U

/** @brief I2C0 SDA pad (unused). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (unused). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (unused). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (unused). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (unused). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

/*---------------------------------------------------------------------------*/
/* PARTS NOT FITTED                                                          */
/*---------------------------------------------------------------------------*/

/*
 * This header defines no TIKU_BOARD_EMMC_PAD_*, USB_PAD_*, PSRAM_PAD_CE or
 * NOR_PAD_*.  The Makefile refuses TIKU_DRV_{EMMC,PSRAM,NOR,USB}_ENABLE for
 * this board, and each of those drivers stops the compile with #error when
 * its pads are missing.  Fitting a part takes a BOARD_CAPS entry in the
 * Makefile and the part's pad block here.
 */

#endif /* TIKU_BOARD_TIKU_BARE_H_ */
