/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_client.h - run one descriptor OUT OF PROCESS.
 *
 * The other half of the deployment bargain: an application written once
 * against tiku_app_descriptor_t and tiku_app_services_t runs
 * linked into the desktop, or through here as its own process over the
 * window session -- the descriptor cannot tell which it got.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CLIENT_H_
#define TIKU_CLIENT_H_

#include "tiku_app.h"

/**
 * @brief Connect, start the descriptor, and pump until it is done.
 *
 * Events and picks arrive through the descriptor's own hooks; tick runs
 * every turn.  @return 0 when the desktop said goodbye or the app closed,
 * 1 when no desktop answered.
 */
/* How long a client waits for the desktop before ticking anyway.  Short
 * because tick is what drives a terminal's pty and a clock's minute. */
#define TIKU_CLIENT_TICK_MS 10

int tiku_client_run(const tiku_app_descriptor_t *app);

/** @brief The same pump over an fd already in hand (a serial link). */
int tiku_client_run_fd(const tiku_app_descriptor_t *app, int fd);

#endif /* TIKU_CLIENT_H_ */
