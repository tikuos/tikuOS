/*
 * Tiku Operating System
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
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
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
        if (!tiku_shell_io_rx_ready()) continue;
        ch = tiku_shell_io_getc();
        if (ch < 0) continue;
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
