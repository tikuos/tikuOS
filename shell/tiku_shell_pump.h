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
 * kernel service, so each such loop calls this once per iteration and aborts
 * when it returns non-zero.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_PUMP_H_
#define TIKU_SHELL_PUMP_H_

#include <stdint.h>

/**
 * @brief One service step for a busy-wait loop.
 *
 * Kicks the watchdog, polls the WiFi receive path and runs the TCP timer and
 * @p periodic at most 8 times a second, each where the build has it, then
 * reads one byte through tiku_shell_net_getc(), dropping any but Ctrl-C.
 *
 * @param periodic Optional protocol housekeeping run with the TCP timer
 *                 (e.g. tiku_kits_net_mqtt_periodic); NULL for none.
 * @return 1 if the byte read was Ctrl-C (the caller aborts), else 0
 */
int tiku_shell_pump_net(void (*periodic)(void));

/** @brief Plain service step: pump with no protocol housekeeping. */
#define tiku_shell_pump()  tiku_shell_pump_net((void (*)(void))0)

#endif /* TIKU_SHELL_PUMP_H_ */
