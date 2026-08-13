#include "tiku_desk_app.h"
#include "tiku_desk_window.h"

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

static void
check(int ok, const char *message)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) {
        failures++;
    }
}

static int
start(void **out, const tiku_desk_app_services_t *services)
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
event(void *opaque, const tiku_desk_event_t *input)
{
    dummy_state_t *s = opaque;

    s->events++;
    return input->type == TIKU_DESK_EVENT_KEY_DOWN;
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
window_event(tiku_desk_window_t *window, const tiku_desk_event_t *input,
             void *context)
{
    (void)window;
    (void)context;
    window_events++;
    return input->type == TIKU_DESK_EVENT_KEY_DOWN;
}

static int
window_close(tiku_desk_window_t *window, void *context)
{
    (void)window;
    (void)context;
    return allow_close;
}

static void
window_destroy(tiku_desk_window_t *window, void *context)
{
    (void)window;
    (void)context;
    window_destroys++;
}

int
main(void)
{
    static const tiku_desk_app_descriptor_t dummy = {
        "test.dummy", "Dummy", start, stop, event, tick
    };
    static const tiku_desk_app_descriptor_t duplicate = {
        "test.dummy", "Duplicate", start, stop, event, tick
    };
    tiku_desk_app_registry_t registry;
    tiku_desk_app_instance_t instance = { 0 };
    tiku_desk_event_t input = { 0 };
    tiku_desk_workspace_t *workspace;
    tiku_desk_window_t *window;
    tiku_desk_window_t *hit = NULL;
    tiku_desk_rect_t frame = { 220, 244, 560, 191 };

    tiku_desk_app_registry_init(&registry);
    check(tiku_desk_app_registry_add(&registry, &dummy) == 0,
          "an app descriptor registers");
    check(tiku_desk_app_registry_add(&registry, &duplicate) < 0,
          "a duplicate stable id is refused");
    check(tiku_desk_app_registry_count(&registry) == 1 &&
          tiku_desk_app_registry_find(&registry, "test.dummy") == &dummy &&
          tiku_desk_app_registry_at(&registry, 0) == &dummy,
          "the registry finds the same descriptor by id and position");

    check(tiku_desk_app_instance_start(&instance, &dummy, NULL) == 0,
          "the runtime starts an application instance");
    input.type = TIKU_DESK_EVENT_KEY_DOWN;
    check(tiku_desk_app_instance_event(&instance, &input) == 1 &&
          state.events == 1,
          "platform-neutral events reach the running app");
    tiku_desk_app_instance_tick(&instance, 42);
    check(state.ticks == 1, "runtime ticks reach the running app");
    tiku_desk_app_instance_stop(&instance);
    check(state.stopped == 1 && !instance.running,
          "stop releases the app exactly once");
    tiku_desk_app_instance_stop(&instance);
    check(state.stopped == 1, "stopping an idle instance is harmless");

    workspace = tiku_desk_workspace_new(NULL);
    window = tiku_desk_workspace_open(workspace, "Test", frame, NULL,
                                      NULL, &state);
    check(window != NULL && tiku_desk_workspace_focused(workspace) == window,
          "an opened window becomes the focused front window");
    check(tiku_desk_workspace_open(workspace, "Again", frame, NULL, NULL,
                                   &state) == window &&
          tiku_desk_workspace_count(workspace) == 1,
          "opening the same owner tag activates one spatial window");
    check(tiku_desk_workspace_hit(workspace, 294, 250, &hit) ==
              TIKU_DESK_HIT_ZOOM && hit == window,
          "ordinary windows expose their zoom control");
    tiku_desk_window_set_size_controls(window, 0, 0);
    check(tiku_desk_workspace_hit(workspace, 294, 250, &hit) ==
              TIKU_DESK_HIT_TAB,
          "window capabilities remove disabled chrome controls");
    tiku_desk_window_set_handlers(window, window_event, window_close,
                                  window_destroy);
    check(tiku_desk_window_send(window, &input) == 1 && window_events == 1,
          "normalized input reaches the window-owned controller");
    allow_close = 0;
    check(!tiku_desk_workspace_request_close(workspace, window) &&
          window->open && window_destroys == 0,
          "a controller may refuse a close request");
    allow_close = 1;
    check(tiku_desk_workspace_request_close(workspace, window) &&
          !window->open && window_destroys == 1,
          "accepted close destroys controller state exactly once");
    tiku_desk_workspace_free(workspace);
    check(window_destroys == 1,
          "freeing the workspace does not destroy a closed window twice");

    return failures ? 1 : 0;
}
