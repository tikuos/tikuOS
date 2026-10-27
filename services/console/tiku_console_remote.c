/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console_remote.c - RAM shell queues and sequenced remote sessions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_console_remote.h"
#include "tiku_console.h"
#include <shell/tiku_shell_io.h>
#include <kernel/cpu/tiku_common.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>
#define OUTPUT_BYTES 4096u
#define INPUT_BYTES 128u
static uint8_t output[OUTPUT_BYTES], input[INPUT_BYTES];
static uint16_t output_head, output_tail, input_head, input_tail;
static uint8_t lost, previous[12], cached[24], previous_length, cached_length;
static uint16_t sequence;
static uint32_t session, build_id, closed_session;
static uint16_t closed_sequence;
static tiku_clock_time_t deadline;
/** @brief Append output without calling or waiting on the transport. */
static void putc_byte(char ch)
{
    uint16_t next = (output_head + 1u) % OUTPUT_BYTES;
    if (next == output_tail) {
        lost = 1;
        return;
    }
    output[output_head] = (uint8_t)ch;
    output_head = next;
}
static uint8_t ready(void)
{
    return 0;
}
static int getc_byte(void)
{
    return -1;
}
static const tiku_shell_io_t backend = {putc_byte, ready, getc_byte,
                                        TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO, TIKU_VFS_CAP_ALL};
/** @brief Decode a little-endian session ID. */
static uint32_t session_id(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/** @brief Cancel a partial command and retire buffered bytes on session loss. */
static void retire(void)
{
    static const uint8_t cancel[] = {3, '\n'};
    input_head = input_tail = output_head = output_tail = 0;
    lost = 0;
    session = sequence = previous_length = cached_length = 0;
    tiku_console_inject_clear();
    (void)tiku_console_inject(cancel, sizeof cancel);
}
void tiku_console_remote_init(uint32_t build)
{
    build_id = build;
    closed_session = closed_sequence = 0;
    session = sequence = previous_length = cached_length = lost = 0;
    input_head = input_tail = output_head = output_tail = 0;
    tiku_shell_io_set_backend(&backend);
}
void tiku_console_remote_pump(void)
{
    if (session && !TIKU_CLOCK_LT(tiku_clock_time(), deadline))
        retire();
    while (input_tail != input_head) {
        if (tiku_console_inject(input + input_tail, 1) != 1)
            break;
        input_tail = (input_tail + 1u) % INPUT_BYTES;
    }
}
size_t tiku_console_remote_request(const uint8_t *p, size_t n, uint8_t out[24])
{
    if (!n || p[0] != TIKU_CONSOLE_REMOTE_MAGIC)
        return 0;
    memset(out, 0, 24);
    out[0] = TIKU_CONSOLE_REMOTE_MAGIC;
    if (n < 8 || n > 12) {
        out[8] = 1;
        return 9;
    }
    memcpy(out, p, 8);
    out[8] = 0;
    uint32_t sid = session_id(p + 2);
    uint16_t seq = (uint16_t)(p[6] | (uint16_t)p[7] << 8);
    if (!sid || !seq || p[1] < 1 || p[1] > 4 || (p[1] != 2 && n != 8) || (p[1] == 2 && n == 8)) {
        out[8] = 1;
        return 9;
    }
    tiku_console_remote_pump();
    if (p[1] == 4 && sid == closed_session && seq == closed_sequence && sid != session)
        return 9;
    if (sid == session && seq == sequence) {
        if (n != previous_length || memcmp(p, previous, n)) {
            out[8] = 4;
            return 9;
        }
        memcpy(out, cached, cached_length);
        deadline = tiku_clock_time() + 30u * TIKU_CLOCK_SECOND;
        return cached_length;
    }
    if (p[1] == 1) {
        if (seq != 1) {
            out[8] = 4;
            return 9;
        }
        if (sid == closed_session) {
            out[8] = 2;
            return 9;
        }
        if (session && session != sid) {
            out[8] = 3;
            return 9;
        }
        if (session) {
            out[8] = 4;
            return 9;
        }
        session = sid;
    } else if (sid != session) {
        out[8] = 2;
        return 9;
    } else if (sequence == UINT16_MAX || seq != sequence + 1u) {
        out[8] = 4;
        return 9;
    }
    size_t length = 9;
    if (p[1] == 1) {
        out[9] = 'C';
        out[10] = '6';
        (void)tiku_common_unique_id(out + 11, 6);
        for (unsigned i = 0; i < 4; i++)
            out[17 + i] = (uint8_t)(build_id >> (8 * i));
        length = 21;
    } else if (p[1] == 2) {
        unsigned available = (input_tail + INPUT_BYTES - input_head - 1u) % INPUT_BYTES;
        if (n - 8 > available) {
            out[8] = 3;
            return 9;
        }
        for (size_t i = 8; i < n; i++) {
            input[input_head] = p[i];
            input_head = (input_head + 1u) % INPUT_BYTES;
        }
    } else if (p[1] == 3) {
        if (lost)
            out[8] = 5;
        while (output_tail != output_head && length < 24) {
            out[length++] = output[output_tail];
            output_tail = (output_tail + 1u) % OUTPUT_BYTES;
        }
    } else {
        closed_session = session;
        closed_sequence = seq;
        retire();
        return length;
    }
    sequence = seq;
    previous_length = (uint8_t)n;
    memcpy(previous, p, n);
    cached_length = (uint8_t)length;
    memcpy(cached, out, length);
    deadline = tiku_clock_time() + 30u * TIKU_CLOCK_SECOND;
    return length;
}
