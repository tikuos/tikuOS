/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_event.h - platform-neutral application input.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_EVENT_H_
#define TIKU_EVENT_H_

#include <stdint.h>

typedef enum {
    TIKU_EVENT_NONE = 0,
    TIKU_EVENT_EXPOSE,
    TIKU_EVENT_CLOSE,
    TIKU_EVENT_RESIZE,
    TIKU_EVENT_KEY_DOWN,
    TIKU_EVENT_KEY_UP,
    TIKU_EVENT_POINTER_DOWN,
    TIKU_EVENT_POINTER_UP,
    TIKU_EVENT_POINTER_MOVE,
    TIKU_EVENT_WHEEL,
    TIKU_EVENT_ACTIVATED,
    TIKU_EVENT_DEACTIVATED,
    TIKU_EVENT_TICK
} tiku_event_type_t;

/* Logical modifiers.  A platform adapter maps its physical keyboard onto
 * these; applications never inspect native mask bits. */
#define TIKU_MOD_SHIFT  0x01u
#define TIKU_MOD_CMD    0x02u
#define TIKU_MOD_OPTION 0x04u
#define TIKU_MOD_CAPS   0x08u

/* Printable keys use their character value.  Non-printing navigation keys
 * occupy the small values already used by Tracker's headless controllers. */
#define TIKU_KEY_UP       1u
#define TIKU_KEY_DOWN     2u
#define TIKU_KEY_LEFT     3u
#define TIKU_KEY_RIGHT    4u
#define TIKU_KEY_HOME     5u
#define TIKU_KEY_END      6u
#define TIKU_KEY_PAGE_UP  7u
#define TIKU_KEY_PAGE_DOWN 8u
#define TIKU_KEY_BACKSPACE 9u
#define TIKU_KEY_DELETE   10u
#define TIKU_KEY_ESCAPE   11u
#define TIKU_KEY_RETURN   12u
#define TIKU_KEY_TAB      13u
#define TIKU_KEY_MENU     14u
#define TIKU_KEY_F2       15u
#define TIKU_KEY_SHIFT    16u
#define TIKU_KEY_CMD      17u
#define TIKU_KEY_OPTION   18u

typedef struct {
    tiku_event_type_t type;
    int64_t                time_us;
    unsigned               modifiers;
    unsigned               key;
    unsigned               button;
    unsigned               repeat;
    char                   text[8];
    int                    x, y;
    int                    dx, dy;
    int                    width, height;
} tiku_event_t;

#endif /* TIKU_EVENT_H_ */
