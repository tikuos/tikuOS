/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_button.c - platform-independent push-button interface implementation.
 *
 * Dispatches indexed button operations to the TIKU_BOARD_BTNn_* macros of
 * the active board.  The switch cases are bounded by TIKU_BOARD_BTN_COUNT, so
 * only macros the board defines are referenced.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_button.h"

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_button_count(void)
{
    return TIKU_BOARD_BTN_COUNT;
}

/*---------------------------------------------------------------------------*/

void
tiku_button_init(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_BTN_COUNT >= 1
    case 0: TIKU_BOARD_BTN1_INIT(); break;
#endif
#if TIKU_BOARD_BTN_COUNT >= 2
    case 1: TIKU_BOARD_BTN2_INIT(); break;
#endif
#if TIKU_BOARD_BTN_COUNT >= 3
    case 2: TIKU_BOARD_BTN3_INIT(); break;
#endif
#if TIKU_BOARD_BTN_COUNT >= 4
    case 3: TIKU_BOARD_BTN4_INIT(); break;
#endif
    default: break;
    }
}

/*---------------------------------------------------------------------------*/

uint8_t
tiku_button_pressed(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_BTN_COUNT >= 1
    case 0: return (uint8_t)(TIKU_BOARD_BTN1_PRESSED() ? 1u : 0u);
#endif
#if TIKU_BOARD_BTN_COUNT >= 2
    case 1: return (uint8_t)(TIKU_BOARD_BTN2_PRESSED() ? 1u : 0u);
#endif
#if TIKU_BOARD_BTN_COUNT >= 3
    case 2: return (uint8_t)(TIKU_BOARD_BTN3_PRESSED() ? 1u : 0u);
#endif
#if TIKU_BOARD_BTN_COUNT >= 4
    case 3: return (uint8_t)(TIKU_BOARD_BTN4_PRESSED() ? 1u : 0u);
#endif
    default: return 0u;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_button_init_all(void)
{
#if TIKU_BOARD_BTN_COUNT > 0
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_BTN_COUNT; i++) {
        tiku_button_init(i);
    }
#endif
}
