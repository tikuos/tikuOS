/*
 * TikuDesktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_run.c - give a loadable application a process of its own.
 *
 * The desktop can open the same file inside itself.  Which of the two
 * happens is where the file was installed, not how it was built.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "tiku_app.h"
#include "tiku_client.h"

int
main(int argc, char **argv)
{
    const tiku_app_descriptor_t *app;
    char why[256];

    if (argc < 2) {
        fprintf(stderr, "usage: tiku-run <application.so>\n");
        return 2;
    }
    app = tiku_app_load(argv[1], why, sizeof why);
    if (app == NULL) {
        fprintf(stderr, "tiku-run: %s: %s\n", argv[1], why);
        return 2;
    }
    return tiku_client_run(app);
}
