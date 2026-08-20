/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_client.h - run one descriptor OUT OF PROCESS.
 *
 * The other half of the deployment bargain: an application written once
 * against tiku_desk_app_descriptor_t and tiku_desk_app_services_t runs
 * linked into the desktop, or through here as its own process over the
 * window session -- the descriptor cannot tell which it got.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_CLIENT_H_
#define TIKU_DESK_CLIENT_H_

#include "tiku_desk_app.h"

/**
 * @brief Connect, start the descriptor, and pump until it is done.
 *
 * Events and picks arrive through the descriptor's own hooks; tick runs
 * every turn.  @return 0 when the desktop said goodbye or the app closed,
 * 1 when no desktop answered.
 */
int tiku_desk_client_run(const tiku_desk_app_descriptor_t *app);

/** @brief The same pump over an fd already in hand (a serial link). */
int tiku_desk_client_run_fd(const tiku_desk_app_descriptor_t *app, int fd);

#endif /* TIKU_DESK_CLIENT_H_ */
