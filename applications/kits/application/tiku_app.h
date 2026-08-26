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
    /**
     * @brief Ask for a window ROLE, not a position.
     *
     * An application does not know the screen, so it cannot centre
     * itself; what it knows is what KIND of window it opened, and the
     * host owns where such a window goes.  ANNOUNCE is the R5 panel
     * rule: centred, in the upper part of the screen, fixed size.
     *
     * Appended, like present(): a host built against the older header
     * leaves it NULL, and an application must treat NULL as "the host
     * decides everything" -- which was always true.
     */
    int (*place)(void *ctx, uint32_t id, int role);
    /**
     * @brief The window's content is now @p w by @p h.
     *
     * For a window whose size follows something that can change under
     * it -- the About box refits when the user picks another face.  The
     * host keeps the corner where it is: a window that re-placed itself
     * would jump under the pointer.  Appended; NULL means the window
     * keeps the size it opened at.
     */
    int (*resize)(void *ctx, uint32_t id, int w, int h);
    /**
     * @brief Ask the SHELL to let the user pick a file, and answer later.
     *
     * The application does not draw a file panel and does not learn to
     * walk a filesystem: it says which kind of question it is asking
     * (TIKU_APP_PICK_OPEN or _SAVE), where to start, and -- saving -- a
     * name to offer.  The shell puts up ITS panel, the one the person
     * already knows, over the namespace the shell already holds.  Which
     * is why a device's files are pickable by an application that never
     * heard of devices: the mount table is the shell's, and so is the
     * panel that walks it.
     *
     * IT DOES NOT BLOCK, and must not: a shell that stopped to ask a
     * question would stop for every other window with it, which is the
     * same reason the kit's alert is drawn by the application over its
     * own surface rather than asked for through here.  The answer
     * arrives later at the descriptor's picked() -- possibly many frames
     * later, and possibly never, because a person who closes the panel
     * has answered nothing and is owed no answer.
     *
     * Appended in the manner of present(): a host that predates it
     * leaves it NULL, and an application offers what it offers through
     * this only when it is there.
     *
     * @return nonzero when the question was put -- or, out of process,
     *         when it was CARRIED: a shell already asking one refuses
     *         it there too, but the refusal cannot ride back through a
     *         return value that has already returned.  Either way the
     *         honest reading is the same: an answer may come, and may
     *         not, and an application that does nothing until picked()
     *         is called needs no more than that.
     */
    int (*pick)(void *ctx, uint32_t id, int mode, const char *start,
                const char *name);
} tiku_app_services_t;

/** @brief Which question pick() asks. */
#define TIKU_APP_PICK_OPEN 0
#define TIKU_APP_PICK_SAVE 1

/** @brief Window roles for place(). */
enum {
    TIKU_APP_PLACE_ANNOUNCE = 1   /* centred, upper third, fixed size */
};

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
    /**
     * @brief The path the person chose, for a pick() this window asked.
     *
     * Called from the shell's own loop rather than from inside pick(),
     * so an application must be able to hear it at any time and must
     * not assume it is still in whatever state it was when it asked.  A
     * cancelled panel never calls this -- the same silence the shell's
     * own askers get, and a window that changes nothing on cancel needs
     * nothing else.
     *
     * Appended: an application that never asks leaves it NULL, and a
     * host must check before calling.
     */
    void (*picked)(void *state, uint32_t window, const char *path);
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
