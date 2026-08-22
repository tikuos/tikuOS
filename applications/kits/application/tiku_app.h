/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app.h - embedded application lifecycle and registry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_APP_H_
#define TIKU_APP_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_event.h"

#define TIKU_APP_MAX 16

struct tiku_dl;

#include "tiku_window.h"

/*
 * The services an application draws THROUGH -- and the reason it never
 * needs to know where it is running.  Linked into the desktop, these are
 * backed by the workspace directly; out of process, by the window session
 * over the desk socket.  Same calls, same order, same data; the process
 * boundary is a deployment property.
 */
typedef struct tiku_app_services {
    void *ctx;
    /** @brief Ask for a window.  @return its id, or 0. */
    uint32_t (*open)(void *ctx, const char *title, int w, int h);
    /** @brief The window's next frame, whole. */
    int (*frame)(void *ctx, uint32_t id, const uint32_t *px, int w, int h);
    /**
     * @brief The window's next frame as what was DRAWN, when it can be.
     *
     * Hand over both: the list recorded while painting and the pixels
     * that painting produced.  Which one goes is not the application's
     * business -- it depends on the link and on whether the list turned
     * out to describe the whole window, and the answer can differ from
     * one frame to the next.
     *
     * Appended to this struct rather than replacing frame(), so an
     * application built against the older header still links and still
     * works; it simply never gets the cheap path.
     *
     * @return 1 when the window was handed over by either road.
     */
    int (*present)(void *ctx, uint32_t id, const struct tiku_dl *dl,
                   const uint32_t *px, int w, int h);
    /** @brief Publish the window's menus, as the plain data they are. */
    int (*menus)(void *ctx, uint32_t id, const tiku_menuset_t *set);
    /** @brief Give the window back. */
    void (*close)(void *ctx, uint32_t id);
} tiku_app_services_t;

typedef struct {
    const char *id;                 /* stable machine-readable identity */
    const char *name;               /* label shown to the user          */
    int  (*start)(void **state, const tiku_app_services_t *services);
    void (*stop)(void *state);
    int  (*event)(void *state, const tiku_event_t *event);
    void (*tick)(void *state, int64_t now_us);
    /* A pick from the window's published menus, wherever the bar
     * lives.  @return nonzero when the application is done. */
    int  (*pick)(void *state, uint32_t window, int command);
    int  (*run)(int argc, char **argv); /* blocking compatibility entry */
} tiku_app_descriptor_t;

/*
 * What a LOADABLE application exports, and the only symbol a loader
 * looks for.  The version lives in the symbol NAME so an incompatible
 * future export is refused by absence rather than by misreading; the
 * numbers inside catch a file built against a different toolkit.
 */
#define TIKU_APP_ABI    1u
#define TIKU_APP_EXPORT "tiku_app_v1"

typedef struct {
    uint32_t                          abi;   /* TIKU_APP_ABI      */
    uint32_t                          size;  /* of the descriptor      */
    const tiku_app_descriptor_t *app;
} tiku_app_export_t;

/**
 * @brief Load the application in the shared object at @p path.
 *
 * @param err Why not, when the answer is NULL; never left unwritten.
 * @return the descriptor, owned by the loaded object, or NULL.
 */
const tiku_app_descriptor_t *tiku_app_load(const char *path,
                                                     char *err,
                                                     size_t max);

typedef struct {
    const tiku_app_descriptor_t *app[TIKU_APP_MAX];
    int                               count;
} tiku_app_registry_t;

typedef struct {
    const tiku_app_descriptor_t *descriptor;
    const tiku_app_services_t   *services;
    void                             *state;
    int                               running;
} tiku_app_instance_t;

void tiku_app_registry_init(tiku_app_registry_t *registry);

/* Add one descriptor.  Duplicate ids and incomplete descriptors are
 * rejected, so application identity remains unambiguous. */
int tiku_app_registry_add(tiku_app_registry_t *registry,
                               const tiku_app_descriptor_t *descriptor);

const tiku_app_descriptor_t *tiku_app_registry_find(
    const tiku_app_registry_t *registry, const char *id);

int tiku_app_registry_count(const tiku_app_registry_t *registry);
const tiku_app_descriptor_t *tiku_app_registry_at(
    const tiku_app_registry_t *registry, int index);

int tiku_app_instance_start(tiku_app_instance_t *instance,
                                 const tiku_app_descriptor_t *descriptor,
                                 const tiku_app_services_t *services);
int tiku_app_instance_event(tiku_app_instance_t *instance,
                                 const tiku_event_t *event);
void tiku_app_instance_tick(tiku_app_instance_t *instance,
                                 int64_t now_us);
void tiku_app_instance_stop(tiku_app_instance_t *instance);

/** Run a registered command-style app through its compatibility entry. */
int tiku_app_run(const tiku_app_descriptor_t *descriptor,
                      int argc, char **argv);

#endif /* TIKU_APP_H_ */
