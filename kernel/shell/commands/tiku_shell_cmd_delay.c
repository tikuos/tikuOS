/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_delay.c - "delay" command implementation
 *
 * Synchronous millisecond wait at clock-tick granularity.  The
 * wait is decomposed into 1-second chunks so the 16-bit
 * tick-deadline arithmetic stays well below the wraparound
 * boundary regardless of TIKU_CLOCK_SECOND.  Each iteration of
 * the inner busy loop polls the active I/O backend so a Ctrl+C
 * (0x03) byte from any transport (UART, telnet, LLM channel)
 * terminates the wait within one tick.
 *
 * Naming: this is intentionally distinct from `sleep`, which
 * configures the kernel's low-power idle mode (LPM3/LPM4).
 * `sleep` *changes power state*; `delay` *just waits*.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_delay.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>

/** Ctrl+C / ETX. */
#define DELAY_CANCEL    0x03

/** Cap a single delay at one minute; longer waits should use the
 * job scheduler so the prompt stays responsive. */
#define DELAY_MAX_MS    60000UL

/**
 * @brief Strict unsigned-decimal parse with overflow guard at
 *        DELAY_MAX_MS.  Returns 1 on success, 0 on parse error or
 *        out-of-range.
 */
static uint8_t
delay_parse_ms(const char *s, unsigned long *out)
{
    unsigned long val = 0;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        unsigned long digit;
        if (*s < '0' || *s > '9') {
            return 0;
        }
        digit = (unsigned long)(*s - '0');
        val = val * 10UL + digit;
        if (val > DELAY_MAX_MS) {
            return 0;
        }
        s++;
    }
    *out = val;
    return 1;
}

/**
 * @brief Wait @p ticks at most one second's worth of clock ticks,
 *        polling for Ctrl+C.  Caller must ensure ticks <=
 *        TIKU_CLOCK_SECOND so the deadline arithmetic does not
 *        wrap.
 *
 * @return 1 if cancelled, 0 if the interval elapsed normally.
 */
static uint8_t
delay_wait_chunk(tiku_clock_time_t ticks)
{
    tiku_clock_time_t deadline;

    if (ticks == 0) {
        return 0;
    }
    deadline = (tiku_clock_time_t)(tiku_clock_time() + ticks);

    while (TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == DELAY_CANCEL) {
                return 1;
            }
            /* Other keystrokes are discarded during the wait. */
        }
    }
    return 0;
}

void
tiku_shell_cmd_delay(uint8_t argc, const char *argv[])
{
    unsigned long ms;
    unsigned long whole_secs;
    tiku_clock_time_t residual_ticks;
    unsigned long i;

    if (argc != 2) {
        SHELL_PRINTF("Usage: delay <ms>  (1..%lu)\n", DELAY_MAX_MS);
        return;
    }
    if (!delay_parse_ms(argv[1], &ms) || ms == 0) {
        SHELL_PRINTF("delay: bad value '%s' (1..%lu ms)\n",
                     argv[1], DELAY_MAX_MS);
        return;
    }

    /* Split <ms> into N whole seconds plus a residual in ticks.
     * Sub-tick requests round up to one tick so the wait always
     * advances time by at least the resolution floor. */
    whole_secs     = ms / 1000UL;
    residual_ticks = (tiku_clock_time_t)
        (((ms - whole_secs * 1000UL) * TIKU_CLOCK_SECOND + 999UL) / 1000UL);

    for (i = 0; i < whole_secs; i++) {
        if (delay_wait_chunk((tiku_clock_time_t)TIKU_CLOCK_SECOND)) {
            SHELL_PRINTF("^C\n");
            return;
        }
    }
    if (residual_ticks > 0) {
        if (delay_wait_chunk(residual_ticks)) {
            SHELL_PRINTF("^C\n");
            return;
        }
    }
}
