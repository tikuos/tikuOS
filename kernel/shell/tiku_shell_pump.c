/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_pump.c - shared busy-wait service step (see header)
 *
 * The body mirrors the device-proven BASIC MQTT pump it replaces:
 * every ingredient (drain-every-call, 8 Hz tcp pacing, SLIP-aware
 * Ctrl-C) was individually debugged on hardware before being
 * centralised here — see the comments at each step for the failure
 * mode it prevents.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_pump.h"
#include <kernel/shell/tiku_shell.h>        /* config, tiku_shell_net_getc */
#include <kernel/shell/tiku_shell_io.h>     /* rx_ready / getc fallback    */
#include <kernel/timers/tiku_clock.h>       /* pacing                      */
#include <kernel/cpu/tiku_watchdog.h>       /* tiku_watchdog_kick          */

/* Is the TCP client actually LINKED into this image?  Mirrors the
 * Makefile: the full-net profile wildcard-compiles tcp.c (whose own
 * gate then takes the tiku_kits_net.h default of enabled), while the
 * lean MIN profile compiles it only when TIKU_KITS_NET_TCP_ENABLE=1
 * is passed on the command line (the MQTT/HTTP opt-ins do that).
 * Testing the header default here would produce an undefined
 * reference on a MIN build without TCP. */
#if defined(TIKU_KIT_NET_ENABLE)
#  if defined(TIKU_KIT_NET_MIN)
#    if defined(TIKU_KITS_NET_TCP_ENABLE) && (TIKU_KITS_NET_TCP_ENABLE + 0)
#      define PUMP_HAS_TCP 1
#    endif
#  else
#    define PUMP_HAS_TCP 1
#  endif
#endif
#ifndef PUMP_HAS_TCP
#define PUMP_HAS_TCP 0
#endif

#if PUMP_HAS_TCP
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#endif
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#include <drivers/wifi/cyw43/whd.h>
#endif

/** @brief ASCII ETX — the console break byte. */
#define PUMP_CTRL_C  0x03

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_shell_pump_net(void (*periodic)(void))
{
    tiku_watchdog_kick();

#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
    /* Drive the WiFi RX drain every call: the cyw43_runner process is
     * starved while the caller busy-waits, so without this the chip's
     * F2 FIFO fills and inbound segments (SYN-ACK, CONNACK, data)
     * never reach the TCP stack — connects would always time out. */
    (void)whd_drain_rx();
#endif

#if PUMP_HAS_TCP
    {
        /* Pace tcp_periodic (+ the protocol hook) to ~8 Hz: it
         * advances connect/retransmit timeouts per call, so a tight
         * loop calling it every iteration would blow through them. */
        static tiku_clock_time_t last;
        tiku_clock_time_t now = tiku_clock_time();
        if ((tiku_clock_time_t)(now - last) >=
            (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 8)) {
            last = now;
            tiku_kits_net_tcp_periodic();
            if (periodic != (void (*)(void))0) {
                periodic();
            }
        }
    }
#else
    (void)periodic;
#endif

    /* Ctrl-C break.  On a SLIP build the console and the IP link
     * share one UART, so read through the SLIP-aware demux: it routes
     * IP frames to the stack and returns only genuine console bytes.
     * The raw getc would misread a payload byte 0x03 as Ctrl-C —
     * aborting the operation with an uncategorised error — and would
     * also steal bytes meant for the TCP stack. */
#if TIKU_SHELL_CMD_SLIP
    if (tiku_shell_net_getc() == PUMP_CTRL_C) {
        return 1;
    }
#else
    if (tiku_shell_io_rx_ready()) {
        if (tiku_shell_io_getc() == PUMP_CTRL_C) {
            return 1;
        }
    }
#endif
    return 0;
}
