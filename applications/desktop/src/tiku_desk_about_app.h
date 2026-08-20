/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_about_app.h - the About panel as ONE descriptor.
 *
 * Written once against the services contract: link it into a desktop or
 * run it through tiku_desk_client_run as its own process -- it cannot
 * tell, which is the point.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_ABOUT_APP_H_
#define TIKU_DESK_ABOUT_APP_H_

#include "tiku_desk_app.h"

extern const tiku_desk_app_descriptor_t tiku_desk_about_app;

#endif /* TIKU_DESK_ABOUT_APP_H_ */
