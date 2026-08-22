/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_session.h - prompt-aware conversation with one device.
 *
 * Sends a command, returns what the device printed before the next prompt, and
 * routes asynchronous `~<path>` subscription lines to a callback wherever they
 * land in the stream.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_SESSION_H_
#define TIKU_DESK_SESSION_H_

#include <stddef.h>
#include "tiku_desk_tx.h"

typedef struct tiku_desk_session tiku_desk_session_t;

/**
 * @brief Called for every `~<path>` line the device emits.
 *
 * Delivered from inside a session call, so handlers must not re-enter the
 * session; queue the path and re-read after the current command returns.
 */
typedef void (*tiku_desk_notify_fn)(const char *path, void *ctx);

/** @brief Wrap an open transport.  Takes ownership of @p tx. */
tiku_desk_session_t *tiku_desk_session_new(tiku_desk_tx_t *tx);

void tiku_desk_session_free(tiku_desk_session_t *s);

/** @brief Install the subscription-line handler (NULL to drop them). */
void tiku_desk_session_on_notify(tiku_desk_session_t *s,
                                 tiku_desk_notify_fn fn, void *ctx);

/**
 * @brief Discard pending bytes and wait for a prompt, so the next command
 *        starts from a known state.  0 on success.
 */
int tiku_desk_session_sync(tiku_desk_session_t *s, int timeout_ms);

/**
 * @brief Run one command line and capture its output.
 *
 * Echo and the trailing prompt are removed; `~` lines are routed to the
 * notify callback rather than into @p out.
 *
 * @param out  Buffer for the reply; always NUL-terminated when non-NULL.
 * @return Bytes placed in @p out, or -1 on timeout or a broken link.
 */
int tiku_desk_session_cmd(tiku_desk_session_t *s, const char *line,
                          char *out, size_t max, int timeout_ms);

/**
 * @brief Service the link without sending anything, so subscription lines
 *        arrive while the UI is idle.
 *
 * @return Number of `~` lines dispatched, or -1 on a broken link.
 */
int tiku_desk_session_poll(tiku_desk_session_t *s, int timeout_ms);

/** @brief Reopen the transport and resynchronise.  0 on success. */
int tiku_desk_session_reconnect(tiku_desk_session_t *s);

/** @brief Endpoint name, for titles and logs. */
const char *tiku_desk_session_name(const tiku_desk_session_t *s);

#endif /* TIKU_DESK_SESSION_H_ */
