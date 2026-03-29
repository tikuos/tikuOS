/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * tiku_shell_cmd_queue.c - "queue" command implementation
 *
 * Lists all pending events in the scheduler event queue, showing
 * the event type and target process for each entry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_queue.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/process/tiku_process.h>

/*---------------------------------------------------------------------------*/
/* EVENT NAME LOOKUP                                                         */
/*---------------------------------------------------------------------------*/

static const char *
event_name(tiku_event_t ev)
{
    switch (ev) {
    case TIKU_EVENT_INIT:       return "INIT";
    case TIKU_EVENT_EXIT:       return "EXIT";
    case TIKU_EVENT_CONTINUE:   return "CONTINUE";
    case TIKU_EVENT_POLL:       return "POLL";
    case TIKU_EVENT_EXITED:     return "EXITED";
    case TIKU_EVENT_FORCE_EXIT: return "FORCE_EXIT";
    case TIKU_EVENT_TIMER:      return "TIMER";
    default:
        if (ev >= TIKU_EVENT_USER) {
            return "USER";
        }
        return "?";
    }
}

/*---------------------------------------------------------------------------*/
/* COMMAND IMPLEMENTATION                                                    */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_queue(uint8_t argc, const char *argv[])
{
    uint8_t len = tiku_process_queue_length();
    uint8_t i;
    tiku_event_t ev;
    struct tiku_process *target;

    (void)argc;
    (void)argv;

    SHELL_PRINTF("Event queue: %u/%u\n", len, TIKU_QUEUE_SIZE);

    if (len == 0) {
        SHELL_PRINTF("  (empty)\n");
        return;
    }

    SHELL_PRINTF(" #  EVENT       TARGET\n");
    SHELL_PRINTF("--  ----------  ---------------\n");

    for (i = 0; i < len; i++) {
        if (tiku_process_queue_peek(i, &ev, &target) != 0) {
            break;
        }
        SHELL_PRINTF("%2u  %-10s  %s\n",
                     i, event_name(ev),
                     target ? (target->name ? target->name : "(null)")
                            : "(broadcast)");
    }
}
