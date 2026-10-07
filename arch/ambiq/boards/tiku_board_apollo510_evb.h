/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510_evb.h - Ambiq Apollo510 EVB board definitions.
 *
 * Pad assignments from the AmbiqSuite apollo510_evb BSP: three active-low user
 * LEDs, the console UART, and the wiring of the eMMC, USB, PSRAM and NOR.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO510_EVB_H_
#define TIKU_BOARD_APOLLO510_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name. */
#define TIKU_BOARD_NAME             "Apollo510 EVB"

/*---------------------------------------------------------------------------*/
/* LEDS                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of user LEDs: LED0-LED2 on pads 165, 89 and 92.
 *
 * TIKU_BOARD_LEDn counts from 1 and backs /dev/led(n-1).  The LEDs are
 * active-low: ON drives the pad low.
 */
#define TIKU_BOARD_LED_COUNT        3

/**
 * @name LED 1 (/dev/led0) on pad 165
 * INIT makes the pad a push-pull output; ON drives it low and OFF high.
 * @{
 */
#define TIKU_BOARD_LED1_PIN         165U
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)
/** @} */

/**
 * @name LED 2 (/dev/led1) on pad 89, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED2_PIN         89U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)
/** @} */

/**
 * @name LED 3 (/dev/led2) on pad 92, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED3_PIN         92U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)
/** @} */

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pads: UART0 TX on pad 30, RX on pad 55, FUNCSEL 4.
 *
 * The UART driver muxes these pads at init, so TIKU_BOARD_UART_PINS_INIT()
 * does nothing.
 */
#define TIKU_BOARD_UART_TX_PIN      30U     /**< UART TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      55U     /**< UART RX pad number. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 4U      /**< pads 30/55 -> UART0 */
/** @brief Empty: the UART driver muxes the console pads itself. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @name Buttons
 * Stubs: INIT does nothing and PRESSED returns 0.  The EVB's BTN0 and BTN1
 * (pads 93 and 94 in the BSP) are not read.
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
 * The (port, pin) GPIO API maps to pad (port - 1) * 8 + pin, with port >= 1
 * and pin 0-7, so this is pad 13, clear of SWO (28) and the console (30/55).
 *
 * @note A bare pad number is not a valid pair: port 0 and pin 13 are both
 *       rejected.  Override either value from the build.
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
/* EMMC (U11) ON SDIO0                                                       */
/*---------------------------------------------------------------------------*/
/*
 * The driver configures D0..CLK and D4..CMD as two contiguous runs of pads
 * (84..88 and 156..160); _Static_asserts in tiku_emmc_arch.c reject a board
 * whose runs are not contiguous.
 *
 * FUNCSEL is fixed per pad by the silicon and differs between the runs:
 * GP84..GP88 reach SDIF0 on FNCSEL 2, GP156..GP160 on FNCSEL 0.  A board
 * that moves the bus sets the FNCSEL values of its own pads.
 */
#define TIKU_BOARD_EMMC_PAD_D0       84U  /**< DAT0; DAT1/2 at 85/86.      */
#define TIKU_BOARD_EMMC_PAD_D3       87U  /**< DAT3 -- end of low run.     */
#define TIKU_BOARD_EMMC_PAD_CLK      88U  /**< CLK; in the low run.        */
#define TIKU_BOARD_EMMC_PAD_D4      156U  /**< DAT4; DAT5/6 at 157/158.    */
#define TIKU_BOARD_EMMC_PAD_D7      159U  /**< DAT7 -- end of high run.    */
#define TIKU_BOARD_EMMC_PAD_CMD     160U  /**< CMD; in the high run.       */
#define TIKU_BOARD_EMMC_FNCSEL_LOW    2U  /**< GP84..GP88   -> SDIF0.      */
#define TIKU_BOARD_EMMC_FNCSEL_HIGH   0U  /**< GP156..GP160 -> SDIF0.      */
/** @brief eMMC RSTn: GP12, the net `SDIO0_RSTn_GP12` on this schematic. */
#define TIKU_BOARD_EMMC_PAD_RST      12U

/*---------------------------------------------------------------------------*/
/* USB RAILS AND HIGH-SPEED REFERENCE                                        */
/*---------------------------------------------------------------------------*/
/*
 * Board pads switch the USB controller's two supply rails.  The high-speed
 * PHY reference is this board's 48 MHz crystal: BOARD_CAPS declares
 * USBHS_CLK_XTAL, and the driver selects XTALHS_DIV2.  The board has no
 * clock-request or reference-clock pad, so it defines no
 * TIKU_BOARD_USB_PAD_CLKREQ or _REFCLK.
 */
#define TIKU_BOARD_USB_PAD_VDDUSB33   91U  /**< 3.3 V rail switch.           */
#define TIKU_BOARD_USB_PAD_VDDUSB0P9  90U  /**< 0.9 V rail switch.           */

/*---------------------------------------------------------------------------*/
/* PSRAM (U14, 64 MB APS25608N) ON MSPI0                                     */
/*---------------------------------------------------------------------------*/
/*
 * MSPI0's data, clock and DQS pads (GP64..GP73) are fixed by the silicon and
 * defined in tiku_psram_arch.c.  The chip select is board wiring: MSPI0 has
 * several CE lines and the part is strapped to one of them.
 */
#define TIKU_BOARD_PSRAM_PAD_CE     199U  /**< MSPI0 CE0. */

/*---------------------------------------------------------------------------*/
/* OCTAL NOR (U12, 8 MB) ON MSPI1                                            */
/*---------------------------------------------------------------------------*/
/*
 * The NOR driver configures D0..DQS as one contiguous run of pads (95..104).
 * GP54 is MSPI1_CE1 in the BSP's naming, but on this board it is wired to the
 * NOR's reset; the part's chip select is CE0 on GP53.
 */
#define TIKU_BOARD_NOR_PAD_D0        95U  /**< D1..D6 follow at 96..101.     */
#define TIKU_BOARD_NOR_PAD_D7       102U  /**< MSPI1 D7.                     */
#define TIKU_BOARD_NOR_PAD_CLK      103U  /**< MSPI1 SCK.                    */
#define TIKU_BOARD_NOR_PAD_DQS      104U  /**< MSPI1 DQS/DM.                 */
#define TIKU_BOARD_NOR_PAD_CE        53U  /**< MSPI1 CE0.                    */
#define TIKU_BOARD_NOR_PAD_RST       54U  /**< BSP calls this CE1; wired RST.*/
#define TIKU_BOARD_NOR_PAD_LSEN     208U  /**< Load switch enable.           */

#endif /* TIKU_BOARD_APOLLO510_EVB_H_ */
