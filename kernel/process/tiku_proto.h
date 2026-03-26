/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_proto.h - Protothreads for lightweight stackless threads
 *
 * Derived from and inspired by the protothreads implementation
 * in Contiki OS (contiki-os.org) by Adam Dunkels.
 *
 * Protothreads provide a blocking context on top of an event-driven system,
 * without the overhead of per-thread stacks. Useful for embedded systems
 * and other memory-constrained environments.
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

#ifndef TIKU_PROTO_H_
#define TIKU_PROTO_H_

#include "tiku_lc.h"

/*---------------------------------------------------------------------------*/
/* CORE DATA STRUCTURES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @struct pt
 * @brief Protothread control structure
 *
 * Each protothread requires a control structure to maintain its state.
 * This structure must be preserved between calls to the protothread function.
 */
struct pt {
  lc_t lc;  /**< Local continuation - stores the thread's execution state */
};

/*---------------------------------------------------------------------------*/
/* RETURN CODES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Thread is waiting for a condition */
#define PT_WAITING 0

/** @brief Thread has yielded and can be resumed */
#define PT_YIELDED 1

/** @brief Thread has exited */
#define PT_EXITED  2

/** @brief Thread has ended normally */
#define PT_ENDED   3

/*---------------------------------------------------------------------------*/
/* INITIALIZATION                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_INIT(pt)
 * @brief Initialize a protothread control structure
 * @param pt Pointer to the protothread control structure
 *
 * Must be called before starting the protothread for the first time.
 * This resets the protothread to its initial state.
 *
 * Example:
 * @code
 *   struct pt my_thread;
 *   PT_INIT(&my_thread);
 * @endcode
 */
#define PT_INIT(pt) LC_INIT((pt)->lc)

/*---------------------------------------------------------------------------*/
/* THREAD DECLARATION AND DEFINITION                                         */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_THREAD(name_args)
 * @brief Declare a protothread function
 * @param name_args Function name and parameters
 * @return char - One of the PT_* return codes
 *
 * All protothread functions must return char and use this macro.
 *
 * Example:
 * @code
 *   PT_THREAD(my_thread(struct pt *pt, int data))
 *   {
 *     PT_BEGIN(pt);
 *     // Thread code here
 *     PT_END(pt);
 *   }
 * @endcode
 */
#define PT_THREAD(name_args) char name_args

/**
 * @def PT_BEGIN(pt)
 * @brief Mark the beginning of a protothread
 * @param pt Pointer to the protothread control structure
 *
 * This macro MUST be the first statement in a protothread function.
 * It sets up the protothread's execution context.
 */
#define PT_BEGIN(pt) {             \
  char PT_YIELD_FLAG = 1;          \
  if(PT_YIELD_FLAG) {;}            \
  LC_RESUME((pt)->lc)

/**
 * @def PT_END(pt)
 * @brief Mark the end of a protothread
 * @param pt Pointer to the protothread control structure
 *
 * This macro MUST be the last statement in a protothread function.
 * It cleans up the protothread and returns PT_ENDED.
 */
#define PT_END(pt)                  \
  LC_END((pt)->lc);                 \
  PT_YIELD_FLAG = 0;                \
  PT_INIT(pt);                      \
  return PT_ENDED;                  \
}

/*---------------------------------------------------------------------------*/
/* BLOCKING OPERATIONS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_WAIT_UNTIL(pt, condition)
 * @brief Block the thread until a condition becomes true
 * @param pt Pointer to the protothread control structure
 * @param condition Boolean expression to evaluate
 *
 * The thread will block and return PT_WAITING until the condition
 * evaluates to true. The condition is checked each time the thread
 * is scheduled.
 *
 * Example:
 * @code
 *   PT_WAIT_UNTIL(pt, sensor_ready == 1);
 * @endcode
 */
#define PT_WAIT_UNTIL(pt, condition)  \
  do {                                \
    LC_SET((pt)->lc);                 \
    if(!(condition)) {                \
      return PT_WAITING;             \
    }                                 \
  } while(0)

/**
 * @def PT_WAIT_WHILE(pt, cond)
 * @brief Block the thread while a condition is true
 * @param pt Pointer to the protothread control structure
 * @param cond Boolean expression to evaluate
 *
 * The thread will block as long as the condition remains true.
 * This is the inverse of PT_WAIT_UNTIL.
 *
 * Example:
 * @code
 *   PT_WAIT_WHILE(pt, buffer_full());
 * @endcode
 */
#define PT_WAIT_WHILE(pt, cond) PT_WAIT_UNTIL((pt), !(cond))

/*---------------------------------------------------------------------------*/
/* HIERARCHICAL PROTOTHREADS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_WAIT_THREAD(pt, thread)
 * @brief Wait for a child protothread to complete
 * @param pt Pointer to the parent protothread control structure
 * @param thread Function call to the child protothread
 *
 * Blocks the parent thread until the child thread exits.
 * The child thread function is called repeatedly until it returns
 * a value indicating completion (PT_EXITED or PT_ENDED).
 */
#define PT_WAIT_THREAD(pt, thread) PT_WAIT_WHILE((pt), PT_SCHEDULE(thread))

/**
 * @def PT_SPAWN(pt, child, thread)
 * @brief Initialize and wait for a child protothread
 * @param pt Pointer to the parent protothread control structure
 * @param child Pointer to the child protothread control structure
 * @param thread Function call to the child protothread
 *
 * This macro initializes a child protothread and waits for it to complete.
 * It's a convenience wrapper that combines PT_INIT and PT_WAIT_THREAD.
 *
 * Example:
 * @code
 *   struct pt child_pt;
 *   PT_SPAWN(pt, &child_pt, child_thread(&child_pt));
 * @endcode
 */
#define PT_SPAWN(pt, child, thread)  \
  do {                               \
    PT_INIT((child));                \
    PT_WAIT_THREAD((pt), (thread));  \
  } while(0)

/*---------------------------------------------------------------------------*/
/* THREAD CONTROL                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_RESTART(pt)
 * @brief Restart the protothread from the beginning
 * @param pt Pointer to the protothread control structure
 *
 * Resets the thread's state and starts execution from PT_BEGIN.
 * Returns immediately with PT_WAITING.
 */
#define PT_RESTART(pt)          \
  do {                          \
    PT_INIT(pt);                \
    return PT_WAITING;          \
  } while(0)

/**
 * @def PT_EXIT(pt)
 * @brief Exit the protothread immediately
 * @param pt Pointer to the protothread control structure
 *
 * Terminates the thread and resets its state.
 * Returns PT_EXITED to indicate abnormal termination.
 */
#define PT_EXIT(pt)             \
  do {                          \
    PT_INIT(pt);                \
    return PT_EXITED;           \
  } while(0)

/*---------------------------------------------------------------------------*/
/* SCHEDULING                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_SCHEDULE(f)
 * @brief Check if a protothread is still running
 * @param f Function call to the protothread
 * @return Non-zero if thread is running, zero if it has exited
 *
 * Used to determine if a protothread should continue to be scheduled.
 * Returns true for PT_WAITING and PT_YIELDED, false for PT_EXITED and PT_ENDED.
 *
 * Example:
 * @code
 *   while(PT_SCHEDULE(my_thread(&pt))) {
 *     // Thread is still running
 *   }
 * @endcode
 */
#define PT_SCHEDULE(f) ((f) < PT_EXITED)

/*---------------------------------------------------------------------------*/
/* COOPERATIVE YIELDING                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @def PT_YIELD(pt)
 * @brief Voluntarily yield execution to other threads
 * @param pt Pointer to the protothread control structure
 *
 * Allows other threads to run. The thread will resume from this
 * point when it is next scheduled.
 *
 * Example:
 * @code
 *   for(i = 0; i < 1000; i++) {
 *     process_item(i);
 *     PT_YIELD(pt);  // Give other threads a chance to run
 *   }
 * @endcode
 */
#define PT_YIELD(pt)             \
  do {                           \
    PT_YIELD_FLAG = 0;           \
    LC_SET((pt)->lc);            \
    if(PT_YIELD_FLAG == 0) {     \
      return PT_YIELDED;         \
    }                            \
  } while(0)

/**
 * @def PT_YIELD_UNTIL(pt, cond)
 * @brief Yield execution until a condition is met
 * @param pt Pointer to the protothread control structure
 * @param cond Boolean expression to evaluate
 *
 * Similar to PT_YIELD, but only resumes when the condition is true.
 * Useful for implementing cooperative waiting without blocking.
 *
 * Example:
 * @code
 *   PT_YIELD_UNTIL(pt, timer_expired());
 * @endcode
 */
#define PT_YIELD_UNTIL(pt, cond)               \
  do {                                         \
    PT_YIELD_FLAG = 0;                         \
    LC_SET((pt)->lc);                          \
    if((PT_YIELD_FLAG == 0) || !(cond)) {      \
      return PT_YIELDED;                       \
    }                                          \
  } while(0)

#endif /* TIKU_PROTO_H_ */

