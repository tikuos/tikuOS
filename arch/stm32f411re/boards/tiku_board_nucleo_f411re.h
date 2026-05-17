/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_board_nucleo_f411re.h - NUCLEO-F411RE board definitions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_NUCLEO_F411RE_H_
#define TIKU_BOARD_NUCLEO_F411RE_H_

#include <stdint.h>
#include <arch/stm32f411re/tiku_gpio_arch.h>

/*---------------------------------------------------------------------------*/
/* BOARD IDENTIFICATION                                                      */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_NAME             "NUCLEO-F411RE"

/*
* User LED: The Nucleo-F411RE has a single user LED (LD2) connected to PA5.
* The other LEDs on the board (LD1 and LD3) cannot be controlled by the user.
*/
#define TIKU_BOARD_LED_COUNT        1
#define TIKU_BOARD_LED1_PORT        1U
#define TIKU_BOARD_LED1_PIN         5U
#define TIKU_BOARD_LED1_INIT()      tiku_stm32f411_gpio_init_output(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_stm32f411_gpio_set(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN, 1U)
#define TIKU_BOARD_LED1_OFF()       tiku_stm32f411_gpio_set(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN, 0U)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_stm32f411_gpio_toggle(TIKU_BOARD_LED1_PORT, TIKU_BOARD_LED1_PIN)

/*---------------------------------------------------------------------------*/
/* Backchannel UART - TX=PA2, RX=PA3 (USART2)                                 */
/*---------------------------------------------------------------------------*/
#define TIKU_BOARD_UART_TX_PORT     1U
#define TIKU_BOARD_UART_TX_PIN      2U
#define TIKU_BOARD_UART_RX_PORT     1U
#define TIKU_BOARD_UART_RX_PIN      3U
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD        115200U
#endif

#define TIKU_BOARD_BTN1_INIT()      do { } while (0)
#define TIKU_BOARD_BTN1_PRESSED()   (0)
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/*---------------------------------------------------------------------------*/
/* Bus-availability gates                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_BOARD_ADC_AVAILABLE    1
#define TIKU_BOARD_I2C_BRW_100K     1
#define TIKU_BOARD_OW_AVAILABLE     1

#define TIKU_BOARD_I2C0_SDA_PORT    2U
#define TIKU_BOARD_I2C0_SDA_PIN     9U
#define TIKU_BOARD_I2C0_SCL_PORT    2U
#define TIKU_BOARD_I2C0_SCL_PIN     8U

#define TIKU_BOARD_SPI0_MISO_PORT   1U
#define TIKU_BOARD_SPI0_MISO_PIN    6U
#define TIKU_BOARD_SPI0_SCK_PORT    1U
#define TIKU_BOARD_SPI0_SCK_PIN     5U
#define TIKU_BOARD_SPI0_MOSI_PORT   1U
#define TIKU_BOARD_SPI0_MOSI_PIN    7U

#define TIKU_BOARD_SPI_BRW_4MHZ     4U
#define TIKU_BOARD_SPI_BRW_2MHZ     8U
#define TIKU_BOARD_SPI_BRW_1MHZ     16U
#define TIKU_BOARD_SPI_BRW_500KHZ   32U

#define TIKU_BOARD_OW_PORT          2U
#define TIKU_BOARD_OW_PIN           6U

#endif /* TIKU_BOARD_NUCLEO_F411RE_H_ */
