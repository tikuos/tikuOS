/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_demo_main.c - the demo descriptor as its own process.
 *
 * Four lines, because that is what the deployment choice should cost.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_demo_app.h"
#include "tiku_client.h"

int
main(void)
{
    return tiku_client_run(&tiku_demo_app);
}
