/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console_remote.h - Bounded remote shell sessions over a message link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CONSOLE_REMOTE_H_
#define TIKU_CONSOLE_REMOTE_H_
#include <stddef.h>
#include <stdint.h>
#define TIKU_CONSOLE_REMOTE_MAGIC 0xa6u
/** @brief Install a RAM shell backend; call before starting the shell process. */
void tiku_console_remote_init(uint32_t build);
/** @brief Service input admission and the session lease in foreground context. */
void tiku_console_remote_pump(void);
/** @brief Handle an 8..12-byte request and render a reply of at most 24 bytes.
 * @note Byte 0 is magic, 1 opcode, 2..5 session, 6..7 sequence, then data.
 * Opcodes 1/2/3/4 open, input, read and close. Sequence starts at 1 and must
 * increase without wrapping. Exact repeats return the cached response.
 * Replies echo the eight-byte header, followed by status and up to 15 bytes.
 * Status 0 succeeds, 1 rejects format, 2 rejects a session, 3 is busy,
 * 4 rejects sequence and 5 reports lost output. A busy request is retryable.
 * A session expires after 30 seconds without an admitted request.
 * @return Reply length, or zero for a packet without the remote marker.
 */
size_t tiku_console_remote_request(const uint8_t *data, size_t length, uint8_t out[24]);
#endif
