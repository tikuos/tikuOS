/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire.c - Platform-independent 1-Wire bus implementation
 *
 * Claims the bus pin through tiku_gpio_claim() and delegates to the
 * architecture-specific 1-Wire driver via the HAL routing header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_onewire.h"
#include "tiku.h"

#ifdef TIKU_BOARD_OW_AVAILABLE  /* Board supports 1-Wire */

#include <hal/tiku_onewire_hal.h>
#include <interfaces/gpio/tiku_gpio_owner.h>

/* The bus pin in the port/pin terms tiku_gpio_claim() takes; RP2350 numbers
 * its pins flat, eight to a port from port 1. */
#if TIKU_BOARD_OW_AVAILABLE && defined(TIKU_BOARD_OW_PIN)
#if defined(PLATFORM_RP2350)
#define OW_PORT ((uint8_t)(TIKU_BOARD_OW_PIN / 8 + 1))
#define OW_PIN ((uint8_t)(TIKU_BOARD_OW_PIN % 8))
#elif defined(TIKU_BOARD_OW_PORT)
#define OW_PORT TIKU_BOARD_OW_PORT
#define OW_PIN TIKU_BOARD_OW_PIN
#endif
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** 1 between a successful init and the matching close. */
static uint8_t configured;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_onewire_is_open(void)
{
    return configured;
}

int
tiku_onewire_init(void)
{
    int rc;

    if (configured) {
        return TIKU_OW_OK;
    }
#ifdef OW_PORT
    if (tiku_gpio_claim(OW_PORT, OW_PIN, "onewire") != 0) {
        return TIKU_OW_ERR_BUSY;
    }
#endif
    rc = tiku_onewire_arch_init();
    if (rc == TIKU_OW_OK) {
        configured = 1;
    } else {
#ifdef OW_PORT
        (void)tiku_gpio_release(OW_PORT, OW_PIN, "onewire");
#endif
    }
    return rc;
}

void
tiku_onewire_close(void)
{
    if (!configured) {
        return;
    }
    tiku_onewire_arch_close();
    configured = 0;
#ifdef OW_PORT
    (void)tiku_gpio_release(OW_PORT, OW_PIN, "onewire");
#endif
}

int
tiku_onewire_reset(void)
{
    return tiku_onewire_arch_reset();
}

void
tiku_onewire_write_bit(uint8_t bit)
{
    tiku_onewire_arch_write_bit(bit);
}

uint8_t
tiku_onewire_read_bit(void)
{
    return tiku_onewire_arch_read_bit();
}

void
tiku_onewire_write_byte(uint8_t byte)
{
    tiku_onewire_arch_write_byte(byte);
}

uint8_t
tiku_onewire_read_byte(void)
{
    return tiku_onewire_arch_read_byte();
}

#endif /* TIKU_BOARD_OW_AVAILABLE */
