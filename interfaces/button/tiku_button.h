/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_button.h - platform-independent push-button interface.
 *
 * An indexed button API sized by the board header's TIKU_BOARD_BTN_COUNT.
 * Indices are zero-based: button 0 maps to TIKU_BOARD_BTN1_*, button 1 to
 * TIKU_BOARD_BTN2_*, and so on, up to four buttons.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BUTTON_H_
#define TIKU_BUTTON_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* DEFAULT BUTTON COUNT                                                      */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BOARD_BTN_COUNT
/** @brief Buttons on the board; 0 when the board header sets none. */
#define TIKU_BOARD_BTN_COUNT    0
#endif

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the number of buttons available on this board.
 * @return Button count defined by the board header.
 */
uint8_t tiku_button_count(void);

/**
 * @brief Configure the button at the given index as an input, with the pull
 *        its board header gives it.
 * @param idx Zero-based button index (0 .. TIKU_BOARD_BTN_COUNT-1); an index
 *            with no button does nothing.
 */
void tiku_button_init(uint8_t idx);

/**
 * @brief Report whether the button at the given index is held down.
 * @param idx Zero-based button index.
 * @return 1 while the button is pressed, 0 when released or for an index
 *         with no button.
 */
uint8_t tiku_button_pressed(uint8_t idx);

/**
 * @brief Initialize all board buttons: tiku_button_init() for every button
 *        TIKU_BOARD_BTN_COUNT defines.
 */
void tiku_button_init_all(void);

#endif /* TIKU_BUTTON_H_ */
