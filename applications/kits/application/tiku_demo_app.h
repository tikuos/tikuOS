/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_demo_app.h - one descriptor, linked in or in its own process.
 *
 * Written once against the services contract: link it into a desktop or
 * run it through tiku_client_run as its own process -- it cannot
 * tell, which is the point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DEMO_APP_H_
#define TIKU_DEMO_APP_H_

#include "tiku_app.h"

extern const tiku_app_descriptor_t tiku_demo_app;

#endif /* TIKU_DEMO_APP_H_ */
