/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_io.inl - shell-I/O helpers for Tiku BASIC.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * The interpreter takes over the shell I/O for the duration of a
 * session.  This piece holds the blocking line reader used by the
 * REPL and the INPUT statement.  It mirrors the host shell's
 * BACKSPACE / local-echo behaviour and treats Ctrl-C as a hard
 * cancel of the line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Read one line from the active shell I/O backend.
 *
 * Blocks in a tight poll loop until a full line arrives or Ctrl-C
 * is received.  Per-character handling mirrors the host shell so
 * BACKSPACE / 0x7F and local echo behave the same.
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
        /* SLIP-aware read: route any frame bytes still trickling in from a
         * prior BROWSE / HTTPGET$ TCP teardown into the IP stack instead of
         * letting them land in the line editor as garbage and wedge the
         * console; take only genuine keystrokes. */
        ch = tiku_shell_net_getc();
        if (ch < 0) continue;
#else
        /* Non-SLIP builds have no net_getc to feed the check-in hang
         * detector, so kick it here: a quiet REPL prompt / INPUT wait is
         * liveness, not a wedge.  Without this an idle prompt warm-resets
         * at TIKU_HANG_THRESHOLD_TICKS (~8 s at 128 Hz). */
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
