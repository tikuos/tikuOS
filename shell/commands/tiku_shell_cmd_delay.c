/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_delay.c - "delay" command implementation.
 *
 * Waits in chunks of at most one second, polling for Ctrl+C, so that every
 * deadline is within the range TIKU_CLOCK_LT() compares correctly on the
 * 16-bit MSP430 clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_delay.h"
#include <shell/tiku_shell.h>
#include <kernel/timers/tiku_clock.h>

/** Ctrl+C / ETX. */
#define DELAY_CANCEL    0x03

/** Longest delay accepted, in milliseconds. */
#define DELAY_MAX_MS    60000UL

/**
 * @brief Parse the decimal digits in @p s into *out.
 * @return 1 on success; 0 for an empty string, a non-digit or a value above
 *         DELAY_MAX_MS
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
 * @brief Wait @p ticks clock ticks, polling for Ctrl+C.
 *
 * @note The caller keeps @p ticks <= TIKU_CLOCK_SECOND, below
 *       TIKU_CLOCK_MAX_INTERVAL, so TIKU_CLOCK_LT() orders the deadline
 *       correctly across a clock wrap.
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

    /* Split <ms> into whole seconds and a remainder in ticks, rounded up to
     * a whole tick. */
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
