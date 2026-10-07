/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510b_evb.h - Ambiq Apollo510 Blue EVB board definitions.
 *
 * Pad assignments from the AmbiqSuite apollo510b_evb BSP for the Apollo510B,
 * whose package adds an EM9305 BLE radio die: user LEDs, the console on
 * UART1, the radio's SPI wiring, and the eMMC, USB and PSRAM wiring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_APOLLO510B_EVB_H_
#define TIKU_BOARD_APOLLO510B_EVB_H_

#include <stdint.h>
#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Human-readable board name. */
#define TIKU_BOARD_NAME             "Apollo510 Blue EVB"

/*---------------------------------------------------------------------------*/
/* LEDS                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Number of user LEDs: LED0-LED2 on pads 11, 19 and 83.
 *
 * TIKU_BOARD_LEDn counts from 1 and backs /dev/led(n-1).  The LEDs are
 * active-low: ON drives the pad low.
 */
#define TIKU_BOARD_LED_COUNT        3

/**
 * @name LED 1 (/dev/led0) on pad 11
 * INIT makes the pad a push-pull output; ON drives it low and OFF high.
 * @{
 */
#define TIKU_BOARD_LED1_PIN         11U
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)
/** @} */

/**
 * @name LED 2 (/dev/led1) on pad 19, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED2_PIN         19U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)
/** @} */

/**
 * @name LED 3 (/dev/led2) on pad 83, driven as LED 1
 * @{
 */
#define TIKU_BOARD_LED3_PIN         83U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)
/** @} */

/*---------------------------------------------------------------------------*/
/* CONSOLE UART                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pads: UART1 TX on pad 12, RX on pad 14, FUNCSEL 5.
 *
 * The build selects UART1 for this board with TIKU_CONSOLE_UART1, and the
 * UART driver muxes these pads at init.
 */
#define TIKU_BOARD_UART_TX_PIN      12U     /**< UART1 TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      14U     /**< UART1 RX pad number. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 5U      /**< pads 12/14 -> UART1 */
/** @brief Empty: the UART driver muxes the console pads itself. */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* BUTTONS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @name Buttons
 * Stubs: INIT does nothing and PRESSED returns 0.  The EVB's BTN0 and BTN1
 * (pads 46 and 29 in the BSP) are not read.
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
 * @brief Default bit-bang output: LED0 on pad 11 (port 2, pin 3).
 *
 * This shares the LED, not the eMMC reset or console signals.
 * Override either value for an external output.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       2U   /**< Port 2 -> pad base 8. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        3U   /**< pin 3 -> pad 11. */
#endif

/*---------------------------------------------------------------------------*/
/* BUS GATES                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * The portable ADC and I2C layers build when their gate macro is defined,
 * whatever its value.  This port's ADC driver works; its I2C and 1-Wire
 * drivers touch no hardware, and their init calls return -1.  No Ambiq code
 * reads the 1-Wire, I2C0 or SPI0 pad macros; the radio's SPI pads are
 * TIKU_BOARD_SPI_* below.
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
/* BLE RADIO (EM9305) ON IOM6 SPI                                            */
/*---------------------------------------------------------------------------*/

/*
 * The EM9305 sits on IOM6 SPI, mode 0 at 16 MHz.  The driver toggles chip
 * select as a plain GPIO, and RDY (INT in the BSP) is the data-ready line.
 * The IOM SPI driver and the EM9305 transport are built only with
 * TIKU_DRV_BLE_EM9305_ENABLE=1, which the Makefile accepts for MCU=apollo510b.
 */
/** @brief IOM instance the radio is wired to (I/O Master 6). */
#define TIKU_BOARD_SPI_IOM_MODULE   6U
/**
 * @name IOM6 SPI pads and their FUNCSEL values
 * @{
 */
#define TIKU_BOARD_SPI_SCK_PIN      61U
#define TIKU_BOARD_SPI_SCK_FUNCSEL  1U
#define TIKU_BOARD_SPI_MOSI_PIN     62U
#define TIKU_BOARD_SPI_MOSI_FUNCSEL 1U
#define TIKU_BOARD_SPI_MISO_PIN     63U
#define TIKU_BOARD_SPI_MISO_FUNCSEL 0U
/** @} */

/** @brief EM9305 chip select, a GPIO output (active low). */
#define TIKU_BOARD_EM9305_CS_PIN    149U
/** @brief EM9305 RDY/INT: data-ready and SPI handshake (GPIO input). */
#define TIKU_BOARD_EM9305_RDY_PIN   117U
/** @brief EM9305 enable/reset strap (GPIO output). */
#define TIKU_BOARD_EM9305_EN_PIN    93U
/** @brief EM9305 12 MHz clock-request line (GPIO output). */
#define TIKU_BOARD_EM9305_CLKREQ_PIN 136U
/** @brief Pad that exports the 32 kHz sleep clock to the EM9305. */
#define TIKU_BOARD_EM9305_CLK32K_PIN 138U
/** @brief FUNCSEL that puts pad 138 on the 32 kHz clock output. */
#define TIKU_BOARD_EM9305_CLK32K_FUNCSEL 1U

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
/*
 * eMMC RSTn is GP13, the net `SDIO0_RSTn_GP13` on this board's schematic.
 * The BSP's SDIO0_RST of 12 is wrong here: GP12 is the console UART's TX.
 */
#define TIKU_BOARD_EMMC_PAD_RST      13U

/*---------------------------------------------------------------------------*/
/* USB RAILS AND HIGH-SPEED REFERENCE                                        */
/*---------------------------------------------------------------------------*/
/*
 * Board pads switch the USB controller's two supply rails.  The board has no
 * high-speed crystal: the PHY reference is the 12 MHz clock of the EM9305
 * die, requested on GP136 and received on GP15.  BOARD_CAPS declares
 * USBHS_CLK_EM9305, which selects that path in the USB driver.
 */
#define TIKU_BOARD_USB_PAD_VDDUSB33   47U  /**< 3.3 V rail switch.           */
#define TIKU_BOARD_USB_PAD_VDDUSB0P9  48U  /**< 0.9 V rail switch.           */
#define TIKU_BOARD_USB_PAD_CLKREQ    136U  /**< Asks the EM9305 for 12 MHz.  */
#define TIKU_BOARD_USB_PAD_REFCLK     15U  /**< Where that clock arrives.    */

/*---------------------------------------------------------------------------*/
/* PSRAM (U14, 64 MB APS25608N) ON MSPI0                                     */
/*---------------------------------------------------------------------------*/
/*
 * MSPI0's data, clock and DQS pads (GP64..GP73) are fixed by the silicon and
 * defined in tiku_psram_arch.c.  The chip select is board wiring: MSPI0 has
 * several CE lines and the part is strapped to one of them.
 */
#define TIKU_BOARD_PSRAM_PAD_CE     199U  /**< MSPI0 CE0. */

#endif /* TIKU_BOARD_APOLLO510B_EVB_H_ */
