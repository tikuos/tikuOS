/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - MSP430 CPU common functions
 *
 * MSP430 busy-wait delays, the die-record unique ID, the reset cause, and
 * MSP430_FR5969_* names for the board's LED and button macros.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU_COMMON_H_
#define TIKU_CPU_COMMON_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"

#ifdef PLATFORM_MSP430

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup TIKU_BOARD_GPIO Board GPIO Definitions
 * @brief MSP430_FR5969_* names for the board's LED and button macros.
 *
 * Each name expands to the TIKU_BOARD_* macro of the same role, which the
 * board header picked by tiku_device_select.h defines.
 * @{
 */

#define MSP430_FR5969_LED1_INIT()       TIKU_BOARD_LED1_INIT()
#define MSP430_FR5969_LED1_ON()         TIKU_BOARD_LED1_ON()
#define MSP430_FR5969_LED1_OFF()        TIKU_BOARD_LED1_OFF()
#define MSP430_FR5969_LED1_TOGGLE()     TIKU_BOARD_LED1_TOGGLE()

#define MSP430_FR5969_LED2_INIT()       TIKU_BOARD_LED2_INIT()
#define MSP430_FR5969_LED2_ON()         TIKU_BOARD_LED2_ON()
#define MSP430_FR5969_LED2_OFF()        TIKU_BOARD_LED2_OFF()
#define MSP430_FR5969_LED2_TOGGLE()     TIKU_BOARD_LED2_TOGGLE()

#define MSP430_FR5969_BTN1_INIT()       TIKU_BOARD_BTN1_INIT()
#define MSP430_FR5969_BTN1_PRESSED()    TIKU_BOARD_BTN1_PRESSED()

#define MSP430_FR5969_BTN2_INIT()       TIKU_BOARD_BTN2_INIT()
#define MSP430_FR5969_BTN2_PRESSED()    TIKU_BOARD_BTN2_PRESSED()

/** @} */ /* End of TIKU_BOARD_GPIO group */

#endif /* PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/* None */

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Busy-wait about @p ms milliseconds.
 * @param ms Number of milliseconds to delay
 */
void tiku_cpu_msp430_delay_ms(unsigned int ms);

/**
 * @brief Busy-wait about @p us microseconds.
 * @param us Number of microseconds to delay (at most 65535)
 */
void tiku_cpu_msp430_delay_us(unsigned int us);

/**
 * @brief Read the MSP430 die-record unique ID into a buffer.
 * @param buf   Destination buffer
 * @param len   Buffer size (up to 8 bytes returned)
 * @return Bytes written: the smaller of @p len and 8, or 0 when @p buf
 *         is NULL
 */
uint8_t tiku_cpu_msp430_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return the reset-cause register value captured at boot.
 *
 * The first call reads SYSRSTIV and every call returns that value.
 *
 * @note A SYSRSTIV read pops the cause it returns: the first call must come
 *       before any other read of the register.
 */
uint16_t tiku_cpu_msp430_reset_reason(void);

#endif /* TIKU_CPU_COMMON_H_ */
