/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer_uart.c - the board's link UART as a wire peer's carrier.
 *
 * The UART link frames the console's envelope on the link UART
 * (hal/tiku_uart_port_hal.h).  A parked UART reads and writes nothing, so
 * the link above it stays open while the peer is off.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_peer_wire.h"
#include <services/link/tiku_link_uart.h>
#include <hal/tiku_uart_port_hal.h>
#include <kernel/ipc/tiku_ipc_backend.h>

#if (TIKU_PEER_ENABLE + 0) && (TIKU_BOARD_HAS_LINK_UART + 0)

/** @brief Console channel the peer's frames carry on the link UART. */
#define PEER_UART_MARKER  0xF4u

static tiku_link_uart_t peer_uart;
static const tiku_uart_port_io_t *peer_port;   /* NULL while parked */

/** @brief Bytes from the UART, or none while it is parked. */
static int
gate_read(void *unused, uint8_t *out, size_t length)
{
    (void)unused;
    if (peer_port == (const tiku_uart_port_io_t *)0) {
        return 0;
    }
    return peer_port->read(peer_port->context, out, length);
}

/** @brief Bytes to the UART; a parked one takes them and sends nothing, so
 *         a frame queued for a peer that is off does not block the link. */
static int
gate_write(void *unused, const uint8_t *data, size_t length)
{
    (void)unused;
    if (peer_port == (const tiku_uart_port_io_t *)0) {
        return (int)length;
    }
    return peer_port->write(peer_port->context, data, length);
}

static const tiku_uart_port_io_t gate_io = { gate_read, gate_write, 0 };

tiku_link_t *
tiku_peer_uart_link(void)
{
    return tiku_link_uart_open(&peer_uart, &gate_io, PEER_UART_MARKER);
}

static uint16_t peer_endpoint;

/** @brief From the UART's interrupt: service the peer's first session, whose
 *         pass reads and writes the link, at the next scheduler pass. */
static void
peer_uart_event(void)
{
    (void)tiku_ipc_service_at(peer_endpoint, 0);
}

void
tiku_peer_uart_attend(uint16_t endpoint)
{
    peer_endpoint = endpoint;
    tiku_uart_link_on_event(peer_uart_event, 0xC0u);
}

int
tiku_peer_uart_carrier(uint8_t up)
{
    if (up != 0u) {
        peer_port = tiku_uart_link_open();
        return (peer_port != (const tiku_uart_port_io_t *)0) ? 0 : -1;
    }
    peer_port = (const tiku_uart_port_io_t *)0;
    tiku_uart_link_close();
    return 0;
}

#endif /* TIKU_PEER_ENABLE && TIKU_BOARD_HAS_LINK_UART */
