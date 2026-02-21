/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lc.h - Local Continuations for lightweight state preservation
 *
 * Derived from and inspired by the local continuations implementation
 * in Contiki OS (contiki-os.org) by Adam Dunkels.
 *
 * Local continuations provide a low-level mechanism to capture and restore
 * a function's execution state. They form the foundation for implementing
 * protothreads and other cooperative multitasking primitives.
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
 * @file lc.h
 * @brief Local Continuations - Lightweight mechanism for saving/restoring execution state
 *
 * Local continuations provide a low-level mechanism to capture and restore
 * a function's execution state (program counter position). They form the
 * foundation for implementing protothreads and other cooperative multitasking
 * primitives in resource-constrained systems.
 * 
 * This implementation supports multiple backends:
 * - Switch-based (default): Uses switch statements and line numbers
 * - Address labels (optional): Uses GCC's address-of-label extension
 * - Custom implementations can be added via LC_CONF_INCLUDE
 */

#ifndef TIKU_LC_H_
#define TIKU_LC_H_

/*---------------------------------------------------------------------------*/
/* CONFIGURATION */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_CONF_INCLUDE
 * @brief Optional configuration to select a specific LC implementation
 * 
 * Define this before including lc.h to use a custom implementation.
 * Common values:
 * - "lc-switch.h" for switch-based implementation
 * - "lc-addrlabels.h" for GCC address labels implementation
 * - Your custom implementation header
 */
#ifdef LC_CONF_INCLUDE
#include LC_CONF_INCLUDE
#else

/*---------------------------------------------------------------------------*/
/* DEFAULT IMPLEMENTATION: SWITCH-BASED LOCAL CONTINUATIONS */
/*---------------------------------------------------------------------------*/

/**
 * @typedef lc_t
 * @brief Type for storing local continuation state
 * 
 * In the switch-based implementation, this stores the line number
 * where execution should resume. The type is unsigned short to
 * minimize memory usage while supporting files up to 65,535 lines.
 */
typedef unsigned short lc_t;

/*---------------------------------------------------------------------------*/
/* CORE MACROS */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_INIT(s)
 * @brief Initialize a local continuation variable
 * @param s The lc_t variable to initialize
 * 
 * Resets the continuation state to the beginning. Must be called
 * before first use of LC_RESUME.
 * 
 * Example:
 * @code
 *   lc_t continuation;
 *   LC_INIT(continuation);
 * @endcode
 */
#define LC_INIT(s) s = 0

/**
 * @def LC_RESUME(s)
 * @brief Resume execution from a saved continuation point
 * @param s The lc_t variable containing the saved state
 * 
 * Begins a switch statement that will jump to the previously saved
 * position (or case 0 if no position was saved). This macro must be
 * paired with LC_END(s).
 * 
 * Example:
 * @code
 *   LC_RESUME(continuation);
 *     // Code that can be resumed
 *     LC_SET(continuation);  // Save position here
 *     return;  // Exit function
 *     // Next call will resume here
 *   LC_END(continuation);
 * @endcode
 * 
 * @warning The code between LC_RESUME and LC_END becomes part of a
 *          switch statement, so certain C constructs may not work as
 *          expected (e.g., variable declarations need to be in blocks).
 */
#define LC_RESUME(s) switch(s) { case 0:

/**
 * @def LC_SET(s)
 * @brief Save the current execution position
 * @param s The lc_t variable to store the position in
 * 
 * Captures the current line number and creates a case label at this
 * position. When LC_RESUME is called with the same variable, execution
 * will jump to this point.
 * 
 * Technical details:
 * - Uses __LINE__ preprocessor macro to get the current line number
 * - Creates a case label with the same line number
 * - Stores the line number in the continuation variable
 * 
 * Example:
 * @code
 *   LC_SET(continuation);  // Save position
 *   if (!ready) {
 *     return WAITING;      // Exit and come back later
 *   }
 *   // Execution resumes here when ready
 * @endcode
 * 
 * @warning Cannot be used inside another switch statement
 * @warning Line numbers must be unique within the function
 */
#define LC_SET(s)                 \
  do {                            \
    (s) = __LINE__; case __LINE__:; \
  } while(0)
/**
 * @def LC_END(s)
 * @brief End the local continuation block
 * @param s The lc_t variable (included for symmetry, not used)
 * 
 * Closes the switch statement started by LC_RESUME. Every LC_RESUME
 * must have a corresponding LC_END.
 * 
 * Example:
 * @code
 *   LC_RESUME(continuation);
 *     // ... continuation code ...
 *   LC_END(continuation);  // Required closing
 * @endcode
 */
#define LC_END(s) }

/*---------------------------------------------------------------------------*/
/* ADVANCED MACROS */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_RESET(s)
 * @brief Reset continuation to start from the beginning
 * @param s The lc_t variable to reset
 * 
 * Alias for LC_INIT, provided for semantic clarity when resetting
 * an already-used continuation.
 */
#define LC_RESET(s) LC_INIT(s)

/**
 * @def LC_IS_RESUMED(s)
 * @brief Check if a continuation has been set
 * @param s The lc_t variable to check
 * @return Non-zero if continuation has been set, zero otherwise
 * 
 * Useful for determining if this is the first call or a resumed call.
 * 
 * Example:
 * @code
 *   if (!LC_IS_RESUMED(continuation)) {
 *     // First-time initialization code
 *   }
 * @endcode
 */
#define LC_IS_RESUMED(s) ((s) != 0)

#endif /* LC_CONF_INCLUDE */

/*---------------------------------------------------------------------------*/
/* USAGE NOTES AND LIMITATIONS */
/*---------------------------------------------------------------------------*/

/**
 * @section lc_usage Usage Guidelines
 * 
 * Local continuations are typically used as building blocks for higher-level
 * abstractions like protothreads. Direct use requires careful attention to:
 * 
 * 1. **State Preservation**: Local variables are NOT preserved across
 *    continuation points. Use static variables or external storage for
 *    data that must persist.
 * 
 * 2. **Nesting Restrictions**: LC_SET cannot be used inside switch statements
 *    or other constructs that interfere with case labels.
 * 
 * 3. **Line Number Uniqueness**: Each LC_SET in a function must be on a
 *    different line to ensure unique case labels.
 * 
 * 4. **Function Scope**: Continuations only work within a single function.
 *    They cannot be used to jump between functions.
 * 
 * @section lc_example Complete Example
 * 
 * @code
 * int example_function(lc_t *lc, int *counter) {
 *   LC_RESUME(*lc);
 *   
 *   // First execution starts here
 *   *counter = 0;
 *   
 *   while (*counter < 10) {
 *     LC_SET(*lc);  // Save position before returning
 *     (*counter)++;
 *     return 0;     // Not done yet
 *   }
 *   
 *   LC_END(*lc);
 *   return 1;       // Done
 * }
 * 
 * // Usage:
 * lc_t lc;
 * int counter;
 * LC_INIT(lc);
 * while (!example_function(&lc, &counter)) {
 *   // Function will be called 10 times
 * }
 * @endcode
 * 
 * @section lc_alternatives Alternative Implementations
 * 
 * For platforms with GCC or compatible compilers, the address-labels
 * implementation may provide better performance and fewer restrictions.
 * Define LC_CONF_INCLUDE to use an alternative:
 * 
 * @code
 * #define LC_CONF_INCLUDE "lc-addrlabels.h"
 * #include "lc.h"
 * @endcode
 */

#endif /* TIKU_LC_H_ */

