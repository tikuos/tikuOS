#include "tiku_app.h"
#include "tiku_window.h"
#include "tiku_gfx.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int events;
    int ticks;
    int stopped;
} dummy_state_t;

static dummy_state_t state;
static int failures;
static int window_events;
static int window_destroys;
static int allow_close;
static int ran;

static void
check(int ok, const char *message)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) {
        failures++;
    }
}

static int
start(void **out, const tiku_app_services_t *services)
{
    (void)services;
    memset(&state, 0, sizeof state);
    *out = &state;
    return 0;
}

static void
stop(void *opaque)
{
    ((dummy_state_t *)opaque)->stopped++;
}

static int
event(void *opaque, const tiku_event_t *input)
{
    dummy_state_t *s = opaque;

    s->events++;
    return input->type == TIKU_EVENT_KEY_DOWN;
}

static void
tick(void *opaque, int64_t now_us)
{
    dummy_state_t *s = opaque;

    if (now_us == 42) {
        s->ticks++;
    }
}

static int
window_event(tiku_window_t *window, const tiku_event_t *input,
             void *context)
{
    (void)window;
    (void)context;
    window_events++;
    return input->type == TIKU_EVENT_KEY_DOWN;
}

static int
window_close(tiku_window_t *window, void *context)
{
    (void)window;
    (void)context;
    return allow_close;
}

static void
window_destroy(tiku_window_t *window, void *context)
{
    (void)window;
    (void)context;
    window_destroys++;
}

static int
run(int argc, char **argv)
{
    ran++;
    return (argc == 1 && argv != NULL) ? 7 : -1;
}

int
main(void)
{
    static const tiku_app_descriptor_t dummy = {
        .id = "test.dummy", .name = "Dummy", .start = start, .stop = stop,
        .event = event, .tick = tick
    };
    static const tiku_app_descriptor_t duplicate = {
        .id = "test.dummy", .name = "Duplicate", .start = start,
        .stop = stop, .event = event, .tick = tick
    };
    static const tiku_app_descriptor_t command = {
        .id = "test.command", .name = "Command", .run = run
    };
    static const tiku_app_descriptor_t incomplete = {
        .id = "test.incomplete", .name = "Incomplete", .start = start,
        .run = run
    };
    tiku_app_registry_t registry;
    tiku_app_instance_t instance = { 0 };
    tiku_event_t input = { 0 };
    tiku_workspace_t *workspace;
    tiku_window_t *window;
    tiku_window_t *hit = NULL;
    tiku_surface_t *surface;
    tiku_rect_t frame = { 220, 244, 560, 191 };
    tiku_rgb_t source[4] = { 1, 2, 3, 4 };
    tiku_rgb_t scaled[16] = { 0 };

    surface = tiku_surface_new(2, 2, TIKU_C_PANEL);
    check(surface != NULL &&
          tiku_surface_resize(surface, 4, 3,
                                   TIKU_C_BACKDROP) == 0 &&
          surface->w == 4 && surface->h == 3 &&
          surface->clip.x == 0 && surface->clip.y == 0 &&
          surface->clip.w == 4 && surface->clip.h == 3 &&
          surface->px[0] == TIKU_C_BACKDROP &&
          surface->px[11] == TIKU_C_BACKDROP,
          "a resized shell redraws against the host's current frame");
    check(tiku_surface_resize(surface, 0, 3,
                                   TIKU_C_BACKDROP) < 0 &&
          surface->w == 4 && surface->h == 3,
          "an invalid resize leaves the current framebuffer intact");
    tiku_surface_free(surface);
    tiku_scale_pixels(scaled, 4, 4, source, 2, 2);
    check(scaled[0] == 1 && scaled[1] == 1 && scaled[4] == 1 &&
          scaled[3] == 2 && scaled[12] == 3 && scaled[15] == 4,
          "integer HiDPI expansion preserves sharp logical pixels");

    tiku_app_registry_init(&registry);
    check(tiku_app_registry_add(&registry, &dummy) == 0,
          "an app descriptor registers");
    check(tiku_app_registry_add(&registry, &duplicate) < 0,
          "a duplicate stable id is refused");
    check(tiku_app_registry_count(&registry) == 1 &&
          tiku_app_registry_find(&registry, "test.dummy") == &dummy &&
          tiku_app_registry_at(&registry, 0) == &dummy,
          "the registry finds the same descriptor by id and position");
    check(tiku_app_registry_add(&registry, &command) == 1,
          "a command-style application registers beside embedded apps");
    check(tiku_app_registry_add(&registry, &incomplete) < 0,
          "a partial embedded lifecycle is refused even with a command entry");
    check(tiku_app_run(
              tiku_app_registry_find(&registry, "test.command"),
              1, (char *[]){ "command", NULL }) == 7 && ran == 1,
          "the registry-selected command entry runs with its arguments");

    check(tiku_app_instance_start(&instance, &dummy, NULL) == 0,
          "the runtime starts an application instance");
    input.type = TIKU_EVENT_KEY_DOWN;
    check(tiku_app_instance_event(&instance, &input) == 1 &&
          state.events == 1,
          "platform-neutral events reach the running app");
    tiku_app_instance_tick(&instance, 42);
    check(state.ticks == 1, "runtime ticks reach the running app");
    tiku_app_instance_stop(&instance);
    check(state.stopped == 1 && !instance.running,
          "stop releases the app exactly once");
    tiku_app_instance_stop(&instance);
    check(state.stopped == 1, "stopping an idle instance is harmless");

    workspace = tiku_workspace_new(NULL);
    window = tiku_workspace_open(workspace, "Test", frame, NULL,
                                      NULL, &state);
    check(window != NULL && tiku_workspace_focused(workspace) == window,
          "an opened window becomes the focused front window");
    check(tiku_workspace_open(workspace, "Again", frame, NULL, NULL,
                                   &state) == window &&
          tiku_workspace_count(workspace) == 1,
          "opening the same owner tag activates one spatial window");
    check(tiku_workspace_hit(workspace, 294, 250, &hit) ==
              TIKU_HIT_ZOOM && hit == window,
          "ordinary windows expose their zoom control");
    tiku_window_set_size_controls(window, 0, 0);
    check(tiku_workspace_hit(workspace, 294, 250, &hit) ==
              TIKU_HIT_TAB,
          "window capabilities remove disabled chrome controls");
    tiku_window_set_handlers(window, window_event, window_close,
                                  window_destroy);
    check(tiku_window_send(window, &input) == 1 && window_events == 1,
          "normalized input reaches the window-owned controller");
    allow_close = 0;
    check(!tiku_workspace_request_close(workspace, window) &&
          window->open && window_destroys == 0,
          "a controller may refuse a close request");
    allow_close = 1;
    check(tiku_workspace_request_close(workspace, window) &&
          !window->open && window_destroys == 1,
          "accepted close destroys controller state exactly once");
    tiku_workspace_free(workspace);
    check(window_destroys == 1,
          "freeing the workspace does not destroy a closed window twice");

    return failures ? 1 : 0;
}
