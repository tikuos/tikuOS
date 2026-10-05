/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_pump.c - shared busy-wait service step.
 *
 * tiku_shell_pump_net(): kicks the watchdog, polls the WiFi radio, paces the
 * TCP timer and reads Ctrl-C from the console, for a command that busy-waits
 * inside one dispatch of the shell process.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_pump.h"
#include <kernel/shell/tiku_shell.h>        /* config, tiku_shell_net_getc */
#include <kernel/timers/tiku_clock.h>       /* pacing                      */
#include <kernel/cpu/tiku_watchdog.h>       /* tiku_watchdog_kick          */

/* PUMP_HAS_TCP is 1 when this image links tiku_kits_net_tcp.c, as the
 * Makefile decides: a full-net build compiles every ipv4 source, and a MIN
 * build compiles tcp.c only with -DTIKU_KITS_NET_TCP_ENABLE=1 (the MQTT,
 * HTTP and IP-link opt-ins). */
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
#if (TIKU_DRV_WIFI_CYW43_ENABLE + 0) || (TIKU_DRV_WIFI_ESP_ENABLE + 0)
#include <interfaces/wireless/tiku_wireless.h>
#define PUMP_HAS_WIFI 1
#endif

/** @brief ASCII ETX — the console break byte. */
#define PUMP_CTRL_C  0x03

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_shell_pump_net(void (*periodic)(void))
{
    tiku_watchdog_kick();

#if defined(PUMP_HAS_WIFI)
    /* Poll the WiFi receive path on every call: the radio's driver process
     * (and on the ESP32-C61 its task) does not run while the caller
     * busy-waits, and inbound segments (SYN-ACK, CONNACK, data) reach the
     * TCP stack only through this poll. */
    (void)tiku_wireless_rx_poll();
#endif

#if PUMP_HAS_TCP
    {
        /* tcp_periodic() advances the connect and retransmit timeouts
         * one step per call, so it and the protocol hook run at most 8
         * times a second. */
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

    /* tiku_shell_net_getc() hands every frame on the console line to its
     * channel and returns only text, so a 0x03 inside a frame is not read
     * as Ctrl-C. */
    if (tiku_shell_net_getc() == PUMP_CTRL_C) {
        return 1;
    }
    return 0;
}
