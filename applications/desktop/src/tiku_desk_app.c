/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_app.c - embedded application lifecycle and registry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_desk_app.h"

#include <string.h>

void
tiku_desk_app_registry_init(tiku_desk_app_registry_t *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof *registry);
    }
}

int
tiku_desk_app_registry_add(tiku_desk_app_registry_t *registry,
                           const tiku_desk_app_descriptor_t *descriptor)
{
    int i;

    if (registry == NULL || descriptor == NULL || descriptor->id == NULL ||
        descriptor->id[0] == '\0' || descriptor->name == NULL ||
        descriptor->name[0] == '\0' ||
        ((descriptor->start == NULL) != (descriptor->stop == NULL)) ||
        (descriptor->start == NULL && descriptor->run == NULL) ||
        registry->count >= TIKU_DESK_APP_MAX) {
        return -1;
    }
    for (i = 0; i < registry->count; i++) {
        if (strcmp(registry->app[i]->id, descriptor->id) == 0) {
            return -1;
        }
    }
    registry->app[registry->count] = descriptor;
    return registry->count++;
}

const tiku_desk_app_descriptor_t *
tiku_desk_app_registry_find(const tiku_desk_app_registry_t *registry,
                            const char *id)
{
    int i;

    for (i = 0; registry != NULL && id != NULL && i < registry->count; i++) {
        if (strcmp(registry->app[i]->id, id) == 0) {
            return registry->app[i];
        }
    }
    return NULL;
}

int
tiku_desk_app_registry_count(const tiku_desk_app_registry_t *registry)
{
    return (registry != NULL) ? registry->count : 0;
}

const tiku_desk_app_descriptor_t *
tiku_desk_app_registry_at(const tiku_desk_app_registry_t *registry, int index)
{
    if (registry == NULL || index < 0 || index >= registry->count) {
        return NULL;
    }
    return registry->app[index];
}

int
tiku_desk_app_instance_start(tiku_desk_app_instance_t *instance,
                             const tiku_desk_app_descriptor_t *descriptor,
                             const tiku_desk_app_services_t *services)
{
    void *state = NULL;

    if (instance == NULL || descriptor == NULL || descriptor->start == NULL ||
        descriptor->stop == NULL || instance->running) {
        return -1;
    }
    if (descriptor->start(&state, services) != 0) {
        return -1;
    }
    instance->descriptor = descriptor;
    instance->services = services;
    instance->state = state;
    instance->running = 1;
    return 0;
}

int
tiku_desk_app_run(const tiku_desk_app_descriptor_t *descriptor,
                  int argc, char **argv)
{
    if (descriptor == NULL || descriptor->run == NULL || argc < 0 ||
        (argc > 0 && argv == NULL)) {
        return -1;
    }
    return descriptor->run(argc, argv);
}

int
tiku_desk_app_instance_event(tiku_desk_app_instance_t *instance,
                             const tiku_desk_event_t *event)
{
    if (instance == NULL || !instance->running || event == NULL ||
        instance->descriptor->event == NULL) {
        return 0;
    }
    return instance->descriptor->event(instance->state, event);
}

void
tiku_desk_app_instance_tick(tiku_desk_app_instance_t *instance,
                            int64_t now_us)
{
    if (instance != NULL && instance->running &&
        instance->descriptor->tick != NULL) {
        instance->descriptor->tick(instance->state, now_us);
    }
}

void
tiku_desk_app_instance_stop(tiku_desk_app_instance_t *instance)
{
    if (instance == NULL || !instance->running) {
        return;
    }
    instance->descriptor->stop(instance->state);
    memset(instance, 0, sizeof *instance);
}
