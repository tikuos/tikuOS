/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_apollo510b_evb.h - Ambiq Apollo510 Blue EVB board definitions
 *
 * The Apollo510 Blue EVB carries the SAME Apollo510 (Cortex-M55) silicon as
 * the base apollo510_evb (so it shares the whole arch backend, linker and
 * register map) but a DIFFERENT board pinout, plus an on-board EM9305 BLE
 * radio the base EVB lacks. Pin assignments from the AmbiqSuite apollo510b_evb
 * BSP:
 *   - User LEDs: LED0 = pad 11, LED1 = pad 19, LED2 = pad 83 (active-low).
 *   - Console UART (COM): TX = pad 12, RX = pad 14 -- on UART1 (funcsel 5),
 *     NOT UART0/30/55 like the base EVB. The Makefile selects the instance via
 *     -DTIKU_CONSOLE_UART1 (see tiku_uart_arch.c).
 *   - Buttons: BTN0 = pad 46, BTN1 = pad 29 (recorded; wired as stubs for now).
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

/** @brief Human-readable board name string exposed via /sys/device. */
#define TIKU_BOARD_NAME             "Apollo510 Blue EVB"

/*---------------------------------------------------------------------------*/
/* LEDs                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief LED definitions for the Apollo510 Blue EVB.
 *
 * The three user LEDs sit on different pads than the base EVB: LED0 = pad 11,
 * LED1 = pad 19, LED2 = pad 83 (the TIKU_BOARD_LED1/LED2/LED3 macros are
 * 1-indexed and surface as /dev/led0, /dev/led1, /dev/led2). All EVB LEDs are
 * active-low, so ON drives the pad low and OFF drives it high.
 */
#define TIKU_BOARD_LED_COUNT        3

/** @brief GPIO pad number for LED 1 (active-low, pad 11). */
#define TIKU_BOARD_LED1_PIN         11U
/** @brief Configure LED 1 pad as a push-pull output. */
#define TIKU_BOARD_LED1_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED1_PIN)
/** @brief Drive LED 1 on (output low — active-low LED). */
#define TIKU_BOARD_LED1_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 0)
/** @brief Drive LED 1 off (output high). */
#define TIKU_BOARD_LED1_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED1_PIN, 1)
/** @brief Toggle LED 1 output state. */
#define TIKU_BOARD_LED1_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED1_PIN)

/** @brief LED 2 (-> /dev/led1): active-low, pad 19. */
#define TIKU_BOARD_LED2_PIN         19U
#define TIKU_BOARD_LED2_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED2_PIN)

/** @brief LED 3 (-> /dev/led2): active-low, pad 83. */
#define TIKU_BOARD_LED3_PIN         83U
#define TIKU_BOARD_LED3_INIT()      tiku_ambiq_gpio_init_output(TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_OFF()       tiku_ambiq_gpio_set(TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_ambiq_gpio_toggle(TIKU_BOARD_LED3_PIN)

/*---------------------------------------------------------------------------*/
/* Console UART pins (TX=12, RX=14 on UART1, funcsel 5)                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UART pin assignments.
 *
 * The Blue EVB routes its J-Link VCOM to UART1 on pads 12 (TX) / 14 (RX),
 * funcsel 5 -- unlike the base EVB's UART0 on 30/55 funcsel 4. The UART
 * instance itself is chosen in the build (-DTIKU_CONSOLE_UART1); these macros
 * carry the pads + funcsel that tiku_uart_arch.c programs into the pin mux.
 */
#define TIKU_BOARD_UART_TX_PIN      12U     /**< UART1 TX pad number. */
#define TIKU_BOARD_UART_RX_PIN      14U     /**< UART1 RX pad number. */
#define TIKU_BOARD_UART_PIN_FUNCSEL 5U      /**< FUNCSEL for pads 12/14 -> UART1. */
/** @brief Board-level UART pin mux init (no-op; done in the UART driver). */
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/*---------------------------------------------------------------------------*/
/* Buttons (recorded from the BSP; wired as stubs for now)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Button stubs — BTN0 = pad 46, BTN1 = pad 29 on this EVB.
 *
 * Kept as no-op / not-pressed placeholders (as on the base EVB) so the console
 * bring-up stays focused; wire the real pads here when button input is needed.
 */
#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
/** @brief Read button 1 state (always 0 — stubbed). */
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
/** @brief Read button 2 state (always 0 — stubbed). */
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bit-bang pin (placeholder — tiku_bitbang demos)                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bit-bang / backscatter port and pin defaults.
 *
 * Same (port,pin) encoding as the base EVB: (port-1)*8 + pin selects pad 13 --
 * a plain GPIO clear of the console UART (pads 12/14) and the LEDs
 * (11/19/83), so the bit-bang self-test has a valid, harmless pin to toggle.
 * Override in the build system to point at a real backscatter pad.
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

/**
 * @brief Peripheral bus availability flags and pin assignments.
 *
 * Identical to the base EVB at this milestone: ADC/1-Wire are stubs, and the
 * I2C/SPI pad macros are placeholders so the interface layer compiles. (The
 * EM9305 BLE radio's SPI pads will be assigned here when BLE bring-up starts.)
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

/*---------------------------------------------------------------------------*/
/* BLE radio (EM9305) + its IOM6 SPI bus                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief EM9305 BLE controller wiring on the Apollo510 Blue EVB.
 *
 * The radio is an EM9305 companion chip on IOM6 SPI (mode 0, up to 16 MHz).
 * Chip-select is a plain GPIO the driver toggles (the IOM's own nCE is not
 * used). RDY (a.k.a. INT) is the data-ready / handshake line. Values verified
 * from the AmbiqSuite apollo510b_evb BSP + am_devices_em9305.c. The IOM SPI
 * master (tiku_spi_arch.c) and the EM9305 transport (tiku_em9305.c) are gated
 * on TIKU_DRV_BLE_EM9305_ENABLE; the base Apollo510 EVB has no radio.
 */
/** @brief IOM instance the radio hangs off (I/O Master 6). */
#define TIKU_BOARD_SPI_IOM_MODULE   6U
/** @brief IOM6 SCK pad + its GPIO funcsel. */
#define TIKU_BOARD_SPI_SCK_PIN      61U
#define TIKU_BOARD_SPI_SCK_FUNCSEL  1U
/** @brief IOM6 MOSI pad + funcsel. */
#define TIKU_BOARD_SPI_MOSI_PIN     62U
#define TIKU_BOARD_SPI_MOSI_FUNCSEL 1U
/** @brief IOM6 MISO pad + funcsel (peripheral input; no GPIO in-enable needed). */
#define TIKU_BOARD_SPI_MISO_PIN     63U
#define TIKU_BOARD_SPI_MISO_FUNCSEL 0U

/** @brief EM9305 chip-select (driven as a plain GPIO, active low). */
#define TIKU_BOARD_EM9305_CS_PIN    149U
/** @brief EM9305 RDY/INT: data-ready + SPI handshake (GPIO input). */
#define TIKU_BOARD_EM9305_RDY_PIN   117U
/** @brief EM9305 enable/reset strap (GPIO output). */
#define TIKU_BOARD_EM9305_EN_PIN    93U
/** @brief EM9305 12 MHz clock-request line (GPIO output). */
#define TIKU_BOARD_EM9305_CLKREQ_PIN 136U
/** @brief EM9305 32 kHz sleep-clock export pad + its funcsel (clock output). */
#define TIKU_BOARD_EM9305_CLK32K_PIN 138U
#define TIKU_BOARD_EM9305_CLK32K_FUNCSEL 3U

#endif /* TIKU_BOARD_APOLLO510B_EVB_H_ */
