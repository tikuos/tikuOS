/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_tiku_bare.h - a custom board carrying Apollo510 silicon and nothing else.
 *
 * Describes the minimum a PCB must declare: a console, and the absence of
 * everything else.  Its empty BOARD_CAPS makes a request for eMMC, PSRAM, NOR or
 * USB fail at make time by name, rather than at run time on the bench.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_TIKU_BARE_H_
#define TIKU_BOARD_TIKU_BARE_H_

#include <arch/ambiq/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* Board identity                                                            */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "TikuOS bare (Apollo510)"

/*---------------------------------------------------------------------------*/
/* LEDs -- none                                                              */
/*---------------------------------------------------------------------------*/
/*
 * Zero is a legitimate answer, and the LED interface already handles it: its
 * dispatch switch is bounded by TIKU_BOARD_LED_COUNT, so with 0 every case
 * compiles out and tiku_led_count() reports 0.  Nothing needs a stub.
 */
#define TIKU_BOARD_LED_COUNT        0

/*---------------------------------------------------------------------------*/
/* Console UART                                                              */
/*---------------------------------------------------------------------------*/
/*
 * THE ONE ASSUMPTION THIS HEADER MAKES, and it is deliberate: a board with no
 * console cannot report that it booted, so the proof this file exists to give
 * would be unobservable.  UART0 on pads 30/55 (FUNCSEL 4) is the Apollo510's
 * conventional UART0 pinout and matches the base EVB.
 *
 * A real custom board OVERRIDES these three lines with whatever it routes.
 * They are the only board wiring here; everything else a
 * driver might want is absent, and absence is now expressible.
 */
#define TIKU_BOARD_UART_TX_PIN      30U     /**< UART0 TX pad. */
#define TIKU_BOARD_UART_RX_PIN      55U     /**< UART0 RX pad. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 4U      /**< FUNCSEL for pads 30/55. */
/** @brief Board-level UART pin mux init (no-op; am_hal muxes at init). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons -- none                                                           */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
/** @brief Read button 1 (always 0 -- no button on this board). */
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
/** @brief Read button 2 (always 0 -- no button on this board). */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin                                                              */
/*---------------------------------------------------------------------------*/
/*
 * The tiku_gpio (port,pin) API encodes an Apollo510 pad as (port-1)*8 + pin,
 * so this pair selects pad 13 -- clear of the console pads above.  Overridable
 * from the build system, like every other board's.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       2U   /**< Port 2 -> pad base 8. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        5U   /**< pin 5 -> pad 13. */
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/** @brief No ADC front end wired. */
#define TIKU_BOARD_ADC_AVAILABLE    0
/** @brief I2C bus rate gate (symbolic -- 100 kHz capable). */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief No 1-Wire device wired. */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire GPIO pad (placeholder; OW_AVAILABLE is 0). */
#define TIKU_BOARD_OW_PIN           13U

/** @brief I2C0 SDA pad (placeholder -- no device fitted). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (placeholder -- no device fitted). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (placeholder -- no device fitted). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (placeholder -- no device fitted). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (placeholder -- no device fitted). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

/*---------------------------------------------------------------------------*/
/* DELIBERATELY ABSENT                                                       */
/*---------------------------------------------------------------------------*/
/*
 * No TIKU_BOARD_EMMC_PAD_*, USB_PAD_*, PSRAM_PAD_CE or NOR_PAD_* appear in
 * this file, and that is the deliverable rather than an omission:
 *
 *   - The Makefile refuses TIKU_DRV_{EMMC,PSRAM,NOR,USB}_ENABLE for this
 *     board, because BOARD_CAPS_tiku_bare is empty.  The build stops with a
 *     message naming the board and the fix.
 *   - Should that gate ever be bypassed, each driver ALSO carries an #error
 *     on its missing pads (added in S3), so the failure is still a compile
 *     error naming the board contract -- never a silent bus at 2 a.m.
 *
 * Adding a part later is one BOARD_CAPS entry plus its pad block here.
 */

#endif /* TIKU_BOARD_TIKU_BARE_H_ */
