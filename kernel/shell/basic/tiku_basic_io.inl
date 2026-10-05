/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_io.inl - shell-I/O helpers for Tiku BASIC.
 *
 * Holds the blocking line reader INPUT uses.  It mirrors the host shell's
 * backspace and echo behaviour and treats Ctrl-C as a hard cancel of the
 * line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Read one line from the active shell I/O backend.
 *
 * Blocks in a tight poll loop until a full line arrives or Ctrl-C
 * is received.  Per-character handling mirrors the host shell so
 * backspace (0x08 or 0x7F) and local echo behave the same.
 *
 * @param buf  Destination buffer (NUL-terminated on return).
 * @param cap  Capacity of @p buf in bytes.
 *
 * @return 0 on '\n' / '\r', -1 if Ctrl-C interrupted.
 */
static int
read_line(char *buf, uint16_t cap)
{
    uint16_t pos = 0;
    while (1) {
        int ch;
#if TIKU_SHELL_CMD_SLIP
        /* tiku_shell_net_getc() hands frame bytes still arriving from an
         * earlier BROWSE or HTTPGET$ connection to their channel and returns
         * only keystrokes; it also kicks the watchdog. */
        ch = tiku_shell_net_getc();
        if (ch < 0) continue;
#else
        /* The kick feeds the check-in hang detector, which otherwise
         * warm-resets an idle INPUT after TIKU_HANG_THRESHOLD_TICKS (8 s at
         * 128 Hz). */
        tiku_watchdog_kick();
        if (!tiku_shell_io_rx_ready()) continue;
        ch = tiku_shell_io_getc();
        if (ch < 0) continue;
#endif
        if (ch == BASIC_CTRL_C) {
            buf[0] = '\0';
            SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
            return -1;
        }
        if (ch == '\r' || ch == '\n') {
            buf[pos] = '\0';
            SHELL_PRINTF("\n");
            return 0;
        }
        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                pos--;
                if (tiku_shell_io_has_echo()) SHELL_PRINTF("\b \b");
            }
            continue;
        }
        if (pos + 1 < cap) {
            buf[pos++] = (char)ch;
            if (tiku_shell_io_has_echo()) {
                char e[2]; e[0] = (char)ch; e[1] = '\0';
                SHELL_PRINTF("%s", e);
            }
        }
    }
}
