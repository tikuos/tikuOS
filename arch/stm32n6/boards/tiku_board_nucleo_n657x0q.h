/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_nucleo_n657x0q.h - NUCLEO-N657X0-Q board wiring.
 *
 * Pin assignments taken from ST's board support package, repo
 * STMicroelectronics/stm32n6xx-nucleo-bsp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BOARD_NUCLEO_N657X0Q_H_
#define TIKU_BOARD_NUCLEO_N657X0Q_H_

#include <arch/stm32n6/tiku_gpio_arch.h>
#include <arch/stm32n6/tiku_stm32n6_regs.h>

#define TIKU_BOARD_NAME             "STM32 Nucleo-144 N657X0-Q"

/* All three user LEDs sit on GPIOG. The numbering here follows the board
 * silkscreen and ST's BSP, so LED3 on the board is LED3 in the shell; the
 * bring-up stub drove LED3, the green one on pin 0. */
#define TIKU_BOARD_LED_COUNT        3
#define TIKU_BOARD_LED_PORT         STM32N6_GPIO_PORT_G

#define TIKU_BOARD_LED1_PIN         8U
#define TIKU_BOARD_LED1_INIT()      tiku_stm32n6_gpio_init_output(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED1_PIN)
#define TIKU_BOARD_LED1_ON()        tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED1_PIN, 1)
#define TIKU_BOARD_LED1_OFF()       tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED1_PIN, 0)
#define TIKU_BOARD_LED1_TOGGLE()    tiku_stm32n6_gpio_toggle(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED1_PIN)

#define TIKU_BOARD_LED2_PIN         10U
#define TIKU_BOARD_LED2_INIT()      tiku_stm32n6_gpio_init_output(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED2_PIN)
#define TIKU_BOARD_LED2_ON()        tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED2_PIN, 1)
#define TIKU_BOARD_LED2_OFF()       tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED2_PIN, 0)
#define TIKU_BOARD_LED2_TOGGLE()    tiku_stm32n6_gpio_toggle(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED2_PIN)

#define TIKU_BOARD_LED3_PIN         0U
#define TIKU_BOARD_LED3_INIT()      tiku_stm32n6_gpio_init_output(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED3_PIN)
#define TIKU_BOARD_LED3_ON()        tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED3_PIN, 1)
#define TIKU_BOARD_LED3_OFF()       tiku_stm32n6_gpio_set(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED3_PIN, 0)
#define TIKU_BOARD_LED3_TOGGLE()    tiku_stm32n6_gpio_toggle(TIKU_BOARD_LED_PORT, TIKU_BOARD_LED3_PIN)

/* USART1 reaches the host as the ST-LINK virtual COM port. The UART driver
 * configures both pins itself, so the board hook has nothing to add. */
#define TIKU_BOARD_UART_PORT        STM32N6_GPIO_PORT_E
#define TIKU_BOARD_UART_TX_PIN      STM32N6_USART1_TX_PIN
#define TIKU_BOARD_UART_RX_PIN      STM32N6_USART1_RX_PIN
#define TIKU_BOARD_UART_PINS_INIT() do { } while (0)

/* The USER button is PC13, active high with an external pull-down. */
#define TIKU_BOARD_BTN1_PORT        2U      /* GPIOC */
#define TIKU_BOARD_BTN1_PIN         13U
#define TIKU_BOARD_BTN1_INIT()      (void)tiku_gpio_arch_set_input(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN)
#define TIKU_BOARD_BTN1_PRESSED()   (tiku_gpio_arch_read(TIKU_BOARD_BTN1_PORT, TIKU_BOARD_BTN1_PIN) == 1)

/* The board has one user button. */
#define TIKU_BOARD_BTN2_INIT()      do { } while (0)
#define TIKU_BOARD_BTN2_PRESSED()   (0)

/* Backscatter, ADC, I2C, one-wire and SPI have no arch backend on this port
 * yet, so nothing is claimed for them. Each turns on with its driver. */
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

#endif /* TIKU_BOARD_NUCLEO_N657X0Q_H_ */
