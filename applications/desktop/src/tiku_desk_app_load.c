/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_app_load.c - taking an application out of a shared object.
 *
 * One exported symbol, checked before anything in it is believed: a file
 * built against another toolkit is refused with a reason rather than run
 * against a descriptor that no longer means what it says.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_app.h"

#include <dlfcn.h>
#include <stdio.h>

const tiku_desk_app_descriptor_t *
tiku_desk_app_load(const char *path, char *err, size_t max)
{
    const tiku_desk_app_export_t *ex;
    void *object;

    if (err != NULL && max > 0u) {
        err[0] = '\0';
    }
    if (path == NULL) {
        snprintf(err, max, "no path");
        return NULL;
    }
    /* RTLD_NOW so a missing symbol is a refusal here, not a crash on the
     * first call; RTLD_LOCAL so one application cannot resolve against
     * another's symbols by accident. */
    object = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (object == NULL) {
        snprintf(err, max, "%s", dlerror());
        return NULL;
    }
    ex = dlsym(object, TIKU_DESK_APP_EXPORT);
    if (ex == NULL) {
        snprintf(err, max, "no %s in %s", TIKU_DESK_APP_EXPORT, path);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->abi != TIKU_DESK_APP_ABI) {
        snprintf(err, max, "built for application interface %u, this is %u",
                 (unsigned)ex->abi, (unsigned)TIKU_DESK_APP_ABI);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->size != (uint32_t)sizeof(tiku_desk_app_descriptor_t)) {
        snprintf(err, max, "descriptor is %u bytes here, %u there",
                 (unsigned)sizeof(tiku_desk_app_descriptor_t),
                 (unsigned)ex->size);
        (void)dlclose(object);
        return NULL;
    }
    if (ex->app == NULL) {
        snprintf(err, max, "exports no application");
        (void)dlclose(object);
        return NULL;
    }
    /* The object stays open for the life of the process: the descriptor
     * and everything it points at live inside it. */
    return ex->app;
}
