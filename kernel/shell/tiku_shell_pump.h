/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_pump.h - one cooperative service step for busy-wait loops
 *
 * Long-running shell/BASIC operations (an MQTT connect, an HTTPS
 * fetch, a blocking receive) busy-wait inside a single command
 * dispatch, which starves every kernel service the scheduler would
 * normally run.  Historically each such loop hand-rolled its own
 * "pump" — kick the watchdog, drain the WiFi radio, pace
 * tcp_periodic, poll for Ctrl-C — and the copies drifted: one used
 * the raw console getc instead of the SLIP-aware demux and misread
 * IP payload bytes as Ctrl-C (the MQTTWAIT$ abort bug).  This is the
 * single shared implementation; busy-wait loops call it once per
 * iteration and abort when it returns non-zero.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_PUMP_H_
#define TIKU_SHELL_PUMP_H_

#include <stdint.h>

/**
 * @brief One cooperative service step for a busy-wait loop.
 *
 * Kicks the watchdog; drains the WiFi radio RX (CYW43 builds — the
 * driver process is starved while we busy-wait, so without this the
 * chip's FIFO fills and inbound segments never reach the stack);
 * paces tiku_kits_net_tcp_periodic() to ~8 Hz (it advances
 * connect/retransmit timeouts PER CALL, so calling it every loop
 * iteration would blow through them) and runs @p periodic at the
 * same paced point; then polls the console for Ctrl-C — through the
 * SLIP-aware demux on shared-UART builds, so an IP payload byte 0x03
 * is never misread as a break (and no stack-bound bytes are stolen).
 *
 * @param periodic Optional protocol housekeeping to run at the paced
 *                 net service point (e.g. tiku_kits_net_mqtt_periodic);
 *                 NULL for none.
 * @return 1 if the user pressed Ctrl-C (caller should abort), else 0
 */
int tiku_shell_pump_net(void (*periodic)(void));

/** @brief Plain service step: pump with no protocol housekeeping. */
#define tiku_shell_pump()  tiku_shell_pump_net((void (*)(void))0)

#endif /* TIKU_SHELL_PUMP_H_ */
