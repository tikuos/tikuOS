/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510_evb.h - Ambiq Apollo 510 EVB board definitions
 *
 * Pin assignments from the AmbiqSuite apollo510_evb BSP:
 *   - User LEDs: LED0 = pad 165, LED1 = pad 89, LED2 = pad 92
 *     (open-drain / active-low in the BSP).
 *   - Console UART (COM): TX = pad 30, RX = pad 55.
 *   - SWO (default console transport): pad 28.
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

/** @brief Human-readable board name string exposed via /sys/device. */
#define TIKU_BOARD_NAME             "Apollo510 EVB"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief LED definitions for the Apollo510 EVB.
 *
 * One primary LED is mapped (LED0, pad 165). LED1 (pad 89) and LED2
 * (pad 92) are also present on the board and can be promoted to
 * TIKU_BOARD_LED2/LED3 in a later pass. All EVB LEDs are active-low,
 * so ON drives the pad low and OFF drives it high.
 */
#define TIKU_BOARD_LED_COUNT        1

/** @brief GPIO pad number for LED 1 (active-low, pad 165). */
#define TIKU_BOARD_LED1_PIN         165U
/** @brief Configure LED 1 pad as a push-pull output. */
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
/** @brief Drive LED 1 on (output low — active-low LED). */
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
/** @brief Drive LED 1 off (output high). */
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
/** @brief Toggle LED 1 output state. */
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* Console UART pins (TX=30, RX=55)                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pin assignments.
 *
 * The default console transport is SWO/ITM (pad 28). The COM-UART
 * pins (TX=30, RX=55) are recorded here for use when a wire-UART
 * backend is selected. Pin mux is performed by am_hal at init time,
 * so the board-level init macro is a no-op.
 */
#define TIKU_BOARD_UART_TX_PIN      30U     /**< UART TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      55U     /**< UART RX pad number. */
/** @brief Board-level UART pin mux init (no-op; handled by am_hal). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons (none wired as plain GPIO yet)                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Button stubs — no buttons are wired as plain GPIO on this EVB.
 *
 * Both BTN macros are no-ops / always-not-pressed placeholders. Wire
 * real pads and update these macros when button input is needed.
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
/** @brief Read button 1 state (always 0 — no button wired). */
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
/** @brief Read button 2 state (always 0 — no button wired). */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (placeholder — tiku_bitbang demos)                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter port and pin defaults.
 *
 * Placeholder values used by tiku_bitbang demos. Override with
 * TIKU_BOARD_BSCAT_PORT / TIKU_BOARD_BSCAT_PIN in the build system
 * to select a real pad.
 */
#ifndef TIKU_BOARD_BSCAT_PORT
#define TIKU_BOARD_BSCAT_PORT       0U   /**< Virtual port for bit-bang pin. */
#endif
#ifndef TIKU_BOARD_BSCAT_PIN
#define TIKU_BOARD_BSCAT_PIN        13U  /**< Bit-bang GPIO pad (placeholder). */
#endif

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Peripheral bus availability flags and pin assignments.
 *
 * ADC and 1-Wire drivers are stubs at this milestone and are marked
 * unavailable. I2C and SPI pin macros are placeholders (TODO: assign
 * real IOM pads) — they are defined so the interface layer compiles
 * without errors on this target.
 */
/** @brief ADC not available at this milestone (stub driver). */
#define TIKU_BOARD_ADC_AVAILABLE    0
/** @brief I2C bus rate gate (symbolic — 100 kHz capable). */
#define TIKU_BOARD_I2C_BRW_100K     1
/** @brief 1-Wire not available at this milestone (stub driver). */
#define TIKU_BOARD_OW_AVAILABLE     0
/** @brief 1-Wire GPIO pad (placeholder). */
#define TIKU_BOARD_OW_PIN           13U

/** @brief I2C0 SDA pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SDA_PIN     0U
/** @brief I2C0 SCL pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_I2C0_SCL_PIN     1U

/** @brief SPI0 MISO pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MISO_PIN    2U
/** @brief SPI0 SCK pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_SCK_PIN     3U
/** @brief SPI0 MOSI pad (placeholder — real IOM pad TBD). */
#define TIKU_BOARD_SPI0_MOSI_PIN    4U

#endif /* TIKU_BOARD_APOLLO510_EVB_H_ */
