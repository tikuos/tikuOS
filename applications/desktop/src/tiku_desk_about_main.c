/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_about_main.c - the About descriptor as its own process.
 *
 * Four lines, because that is what the deployment choice should cost.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desk_about_app.h"
#include "tiku_desk_client.h"

int
main(void)
{
    return tiku_desk_client_run(&tiku_desk_about_app);
}
