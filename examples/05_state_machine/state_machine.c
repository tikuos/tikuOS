/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * state_machine.c - TikuOS Example 05: Event-driven state machine
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file    state_machine.c
 * @brief   TikuOS Example 05 - Event-Driven State Machine
 *
 * Implements a classic state machine pattern on top of TikuOS
 * processes.  The process cycles through IDLE -> ACTIVE -> COOLDOWN
 * using timer events to drive transitions.
 *
 * LED patterns show the current state:
 *   IDLE     -- both LEDs off, waits 3 seconds then enters ACTIVE
 *   ACTIVE   -- LED1 blinks fast (250 ms), runs for 5 blinks then COOLDOWN
 *   COOLDOWN -- LED2 on solid, waits 2 seconds then back to IDLE
 *
 * Hardware: MSP430FR5969 LaunchPad (LED1 = P1.0, LED2 = P4.6)
 *
 * What you learn:
 *   - Structuring a process as a state machine
 *   - Using timers with different intervals per state
 *   - Keeping state in static variables across yields
 */

#include "tiku.h"

#if TIKU_EXAMPLE_STATE_MACHINE

/*--------------------------------------------------------------------------*/
/* States                                                                    */
/*--------------------------------------------------------------------------*/
enum app_state {
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_COOLDOWN
};

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(state_machine, "StateMachine");

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer sm_timer;

TIKU_PROCESS_THREAD(state_machine, ev, data)
{
    static enum app_state state;
    static int blink_count;

    TIKU_PROCESS_BEGIN();

    tiku_common_led1_init();
    tiku_common_led2_init();
    state = STATE_IDLE;

    /* IDLE: wait 3 seconds before starting */
    tiku_timer_set_event(&sm_timer, TIKU_CLOCK_SECOND * 3);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        switch (state) {
        case STATE_IDLE:
            /* Transition to ACTIVE */
            tiku_common_led1_off();
            tiku_common_led2_off();
            state = STATE_ACTIVE;
            blink_count = 0;
            tiku_timer_set_event(&sm_timer, TIKU_CLOCK_SECOND / 4);
            break;

        case STATE_ACTIVE:
            tiku_common_led1_toggle();
            blink_count++;
            if (blink_count >= 10) {  /* 5 full on/off cycles */
                /* Transition to COOLDOWN */
                tiku_common_led1_off();
                tiku_common_led2_on();
                state = STATE_COOLDOWN;
                tiku_timer_set_event(&sm_timer, TIKU_CLOCK_SECOND * 2);
            } else {
                tiku_timer_reset(&sm_timer);
            }
            break;

        case STATE_COOLDOWN:
            /* Transition back to IDLE */
            tiku_common_led2_off();
            state = STATE_IDLE;
            tiku_timer_set_event(&sm_timer, TIKU_CLOCK_SECOND * 3);
            break;
        }
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&state_machine);

#endif /* TIKU_EXAMPLE_STATE_MACHINE */
