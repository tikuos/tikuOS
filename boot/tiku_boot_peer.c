/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_peer.c - the peer table of a board with one peer on a UART.
 *
 * The side with board controls (BOARD_CAPS BOARD_CTL) is the supervisor and
 * switches the peer; the other is the worker.  The carrier is the link UART,
 * or the console line where the board puts its console on that UART.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_boot_peer.h"

#if (TIKU_PEER_ENABLE + 0)

#if (TIKU_BOARD_HAS_BOARD_CTL + 0)
#include <interfaces/board/tiku_board_ctl.h>
#endif
#if (TIKU_PEER_ON_CONSOLE + 0)
#include <services/link/tiku_link_console.h>
#endif
/* The Makefile's view of the carrier (a -D every file sees) against the
 * board header's. */
#if (TIKU_PEER_ON_CONSOLE + 0) != (TIKU_BOARD_PEER_ON_CONSOLE + 0)
#error "TIKU_PEER_ON_CONSOLE and the board's console selection disagree"
#endif

/** @brief Wire frame on this carrier: its largest copied message is 88 bytes. */
#ifndef TIKU_BOARD_PEER_FRAME
#define TIKU_BOARD_PEER_FRAME  128u
#endif

#if TIKU_BOARD_PEER_FRAME > TIKU_IPC_WIRE_FRAME
#error "TIKU_BOARD_PEER_FRAME is larger than the sessions' TIKU_IPC_WIRE_FRAME"
#endif

/** @brief Message bytes a second the carrier moves: a UART byte is ten
 *         bits, and a quarter of the line goes to framing and escapes. */
#ifdef TIKU_BOARD_LINK_BAUD
#define PEER_BYTE_RATE  ((uint32_t)(TIKU_BOARD_LINK_BAUD) * 3u / 40u)
#else
#define PEER_BYTE_RATE  0u
#endif

/** @brief Seconds a started peer may stay silent before it counts as lost. */
#ifndef TIKU_BOARD_PEER_START_S
#define TIKU_BOARD_PEER_START_S  10u
#endif

#define PEER_SERVICE(name, channel)  { name, channel },
static const tiku_peer_service_t peer_services[] = {
    TIKU_BOOT_PEER_SERVICES(PEER_SERVICE)
};

/*---------------------------------------------------------------------------*/
/* CARRIER                                                                   */
/*---------------------------------------------------------------------------*/

#if (TIKU_PEER_ON_CONSOLE + 0)
/** @brief Console channel the peer's frames ride, beside the shell's text. */
#define PEER_CONSOLE_MARKER  0xF4u

static tiku_link_console_t peer_console;
static uint8_t peer_console_frame[TIKU_IPC_WIRE_FRAME + 1u +
                                  TIKU_LINK_CONSOLE_OVERHEAD];

/** @brief The console line as the carrier; it is up from boot. */
static tiku_link_t *
peer_link(void)
{
    return tiku_link_console_open(&peer_console, PEER_CONSOLE_MARKER,
                                  peer_console_frame,
                                  sizeof(peer_console_frame));
}
#define PEER_CARRIER       0
#define PEER_ATTEND        0
#define PEER_SPEED         0
#define PEER_HEARD         0
#define PEER_FIRST_SPEED   0u
#define PEER_CARRIER_NAME  "console"
#else
#define peer_link          tiku_peer_uart_link
#define PEER_CARRIER       tiku_peer_uart_carrier
#define PEER_ATTEND        tiku_peer_uart_attend
#define PEER_SPEED         tiku_peer_uart_speed
#define PEER_HEARD         tiku_peer_uart_heard
#define PEER_FIRST_SPEED   ((uint32_t)(TIKU_BOARD_LINK_BAUD))
#define PEER_CARRIER_NAME  "uart"
#endif

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

#if (TIKU_BOARD_HAS_BOARD_CTL + 0)
/** @brief Switch the peer's supply through the board controls. */
static int
peer_power(uint8_t on)
{
    return (tiku_board_ctl_coproc_power(on) == TIKU_BOARD_CTL_OK) ? 0 : -1;
}

/** @brief Pulse the peer's reset line through the board controls. */
static int
peer_reset(void)
{
    return (tiku_board_ctl_coproc_reset() == TIKU_BOARD_CTL_OK) ? 0 : -1;
}

/** @brief 1 while the board drives the peer's supply on. */
static uint8_t
peer_powered(void)
{
    tiku_board_ctl_state_t s;

    tiku_board_ctl_state(&s);
    return s.coproc_power;
}

static const tiku_peer_wire_board_t peer_board = {
    peer_link, PEER_CARRIER, peer_power, peer_reset, peer_powered, PEER_ATTEND,
    PEER_SPEED, PEER_HEARD
};
/* A board that is one computer with its peer has it started at boot. */
#if (TIKU_BOARD_PEER_AUTOSTART + 0)
#define PEER_FLAGS  (TIKU_PEER_F_SUPERVISOR | TIKU_PEER_F_AUTOSTART)
#else
#define PEER_FLAGS  TIKU_PEER_F_SUPERVISOR
#endif
#else
static const tiku_peer_wire_board_t peer_board = {
    peer_link, PEER_CARRIER, 0, 0, 0, PEER_ATTEND, PEER_SPEED, PEER_HEARD
};
#define PEER_FLAGS  0u
#endif

/*---------------------------------------------------------------------------*/
/* TABLE                                                                     */
/*---------------------------------------------------------------------------*/

static const tiku_peer_t peer;

tiku_peer_wire_t tiku_boot_peer_wire = {
    &peer, &peer_board, TIKU_BOARD_PEER_FRAME,
    (tiku_clock_time_t)(TIKU_BOARD_PEER_START_S * TIKU_CLOCK_SECOND),
    PEER_BYTE_RATE, PEER_FIRST_SPEED
};

static const tiku_peer_t peer = {
    TIKU_BOARD_PEER_NAME, PEER_CARRIER_NAME, &tiku_peer_wire_ops,
    &tiku_boot_peer_wire, peer_services,
    (uint8_t)(sizeof(peer_services) / sizeof(peer_services[0])), PEER_FLAGS
};

const tiku_peer_t *const tiku_peer_table[] = { &peer };
const uint8_t tiku_peer_count = 1u;

#endif /* TIKU_PEER_ENABLE */
