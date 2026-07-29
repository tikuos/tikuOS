/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_pump.h - one cooperative service step for busy-wait loops.
 *
 * A long operation that busy-waits inside one command dispatch starves every
 * kernel service, so each such loop calls this once per iteration and aborts when
 * it returns non-zero.  One shared implementation, because hand-rolled copies drifted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_PUMP_H_
#define TIKU_SHELL_PUMP_H_

#include <stdint.h>

/**
 * @brief One cooperative service step for a busy-wait loop.
 *
 * Kicks the watchdog, drains the WiFi radio RX, paces
 * tiku_kits_net_tcp_periodic() to ~8 Hz along with @p periodic, then polls the
 * console for Ctrl-C through the SLIP-aware demux.
 *
 * @note The RX drain matters because the driver process is starved during a
 *       busy-wait, so the chip's FIFO would fill and inbound segments never
 *       reach the stack.  tcp_periodic advances connect/retransmit timeouts PER
 *       CALL, so calling it every iteration would blow through them.  The
 *       SLIP-aware poll keeps an IP payload byte 0x03 from reading as a break.
 * @param periodic Optional protocol housekeeping to run at the paced
 *                 net service point (e.g. tiku_kits_net_mqtt_periodic);
 *                 NULL for none.
 * @return 1 if the user pressed Ctrl-C (caller should abort), else 0
 */
int tiku_shell_pump_net(void (*periodic)(void));

/** @brief Plain service step: pump with no protocol housekeeping. */
#define tiku_shell_pump()  tiku_shell_pump_net((void (*)(void))0)

#endif /* TIKU_SHELL_PUMP_H_ */
