#include "tiku_desk_app.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int events;
    int ticks;
    int stopped;
} dummy_state_t;

static dummy_state_t state;
static int failures;

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

    return failures ? 1 : 0;
}
