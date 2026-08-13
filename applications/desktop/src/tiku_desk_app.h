/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desk_app.h - embedded application lifecycle and registry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DESK_APP_H_
#define TIKU_DESK_APP_H_

#include <stdint.h>

#include "tiku_desk_event.h"

#define TIKU_DESK_APP_MAX 16

/* Runtime services are deliberately opaque here.  The lifecycle contract
 * does not expose runtime state, and services can grow behind this type. */
typedef struct tiku_desk_app_services tiku_desk_app_services_t;

typedef struct {
    const char *id;                 /* stable machine-readable identity */
    const char *name;               /* label shown to the user          */
    int  (*start)(void **state, const tiku_desk_app_services_t *services);
    void (*stop)(void *state);
    int  (*event)(void *state, const tiku_desk_event_t *event);
    void (*tick)(void *state, int64_t now_us);
    int  (*run)(int argc, char **argv); /* blocking compatibility entry */
} tiku_desk_app_descriptor_t;

typedef struct {
    const tiku_desk_app_descriptor_t *app[TIKU_DESK_APP_MAX];
    int                               count;
} tiku_desk_app_registry_t;

typedef struct {
    const tiku_desk_app_descriptor_t *descriptor;
    const tiku_desk_app_services_t   *services;
    void                             *state;
    int                               running;
} tiku_desk_app_instance_t;

void tiku_desk_app_registry_init(tiku_desk_app_registry_t *registry);

/* Add one descriptor.  Duplicate ids and incomplete descriptors are
 * rejected, so application identity remains unambiguous. */
int tiku_desk_app_registry_add(tiku_desk_app_registry_t *registry,
                               const tiku_desk_app_descriptor_t *descriptor);

const tiku_desk_app_descriptor_t *tiku_desk_app_registry_find(
    const tiku_desk_app_registry_t *registry, const char *id);

int tiku_desk_app_registry_count(const tiku_desk_app_registry_t *registry);
const tiku_desk_app_descriptor_t *tiku_desk_app_registry_at(
    const tiku_desk_app_registry_t *registry, int index);

int tiku_desk_app_instance_start(tiku_desk_app_instance_t *instance,
                                 const tiku_desk_app_descriptor_t *descriptor,
                                 const tiku_desk_app_services_t *services);
int tiku_desk_app_instance_event(tiku_desk_app_instance_t *instance,
                                 const tiku_desk_event_t *event);
void tiku_desk_app_instance_tick(tiku_desk_app_instance_t *instance,
                                 int64_t now_us);
void tiku_desk_app_instance_stop(tiku_desk_app_instance_t *instance);

/** Run a registered command-style app through its compatibility entry. */
int tiku_desk_app_run(const tiku_desk_app_descriptor_t *descriptor,
                      int argc, char **argv);

#endif /* TIKU_DESK_APP_H_ */
