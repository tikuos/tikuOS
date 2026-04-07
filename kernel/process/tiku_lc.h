/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lc.h - Local continuations for lightweight stackless threads
 *
 * Local continuations capture and restore a function's execution state
 * via case labels embedded in a switch statement.  They form the
 * foundation for higher-level cooperative concurrency primitives such
 * as protothreads.  An optional NVM-backed variant lets a continuation
 * survive a power cycle so that battery-free / intermittent-computing
 * applications can resume from their last checkpoint instead of
 * restarting from the beginning.
 *
 * Derived from and inspired by the local-continuations implementation
 * in Contiki OS (contiki-os.org) by Adam Dunkels.
 *
 * Typical usage:
 * @code
 *   lc_t lc;
 *   LC_INIT(lc);
 *
 *   for (;;) {
 *       LC_RESUME(lc);
 *       do_step();
 *       LC_SET(lc);     // save the line number to resume from
 *       return;         // yield back to the caller
 *       LC_END(lc);
 *   }
 * @endcode
 *
 * Persistent (NVM-backed) variant:
 * @code
 *   tiku_lc_persist_init();
 *   tiku_lc_persist_register("tls");
 *   ...
 *   LC_RESUME_PERSISTENT(pt->lc, "tls");
 *       step_one();
 *       LC_SET_PERSISTENT(pt->lc);   // checkpoint survives power loss
 *       return;
 *       step_two();
 *   LC_END(pt->lc);
 *   LC_CLEAR_PERSISTENT("tls");      // done -- clear the NVM entry
 * @endcode
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

#ifndef TIKU_LC_H_
#define TIKU_LC_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Optional override for the local-continuation backend
 *
 * Define before including this header to substitute a custom backend
 * for the default switch/case implementation.  Common values are
 * @c "lc-switch.h" (the default) and @c "lc-addrlabels.h" for the
 * GCC computed-goto variant on toolchains that support it.
 */
#ifdef LC_CONF_INCLUDE
#include LC_CONF_INCLUDE
#else

/*---------------------------------------------------------------------------*/
/* DEFAULT IMPLEMENTATION: SWITCH-BASED LOCAL CONTINUATIONS                  */
/*---------------------------------------------------------------------------*/

/**
 * @typedef lc_t
 * @brief Storage type for a saved local-continuation point
 *
 * Holds the source line number where execution should resume on the
 * next call.  The default width is uint16_t (max 65 535 lines per
 * protothread function).  Define @c TIKU_LC_COMPACT before including
 * this header to use uint8_t instead, which saves one byte per
 * protothread but caps each protothread function at 255 source lines.
 */
#ifdef TIKU_LC_COMPACT
typedef uint8_t  lc_t;
#define TIKU_LC_MAX 255
#else
typedef uint16_t lc_t;
#define TIKU_LC_MAX 65535
#endif

/*---------------------------------------------------------------------------*/
/* CORE MACROS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_INIT(s)
 * @brief Reset a local continuation to its initial state
 *
 * Must be called before the first LC_RESUME on @p s.  After init the
 * next LC_RESUME enters at case 0 (the top of the protothread body).
 *
 * @param s lc_t variable to reset
 */
#define LC_INIT(s) s = 0

/**
 * @def LC_RESUME(s)
 * @brief Resume execution from a previously saved continuation point
 *
 * Opens a switch statement that jumps to the case label saved by the
 * most recent LC_SET on @p s, or to case 0 on the first call.  Must
 * be paired with a matching LC_END(s).
 *
 * @warning Code between LC_RESUME and LC_END lives inside a switch
 *          statement, so constructs that interfere with case labels
 *          (nested switches, certain variable declarations) will not
 *          compile or will behave unexpectedly.
 *
 * @param s lc_t variable holding the saved state
 */
#define LC_RESUME(s) switch(s) { case 0:

/**
 * @def LC_SET(s)
 * @brief Save the current source line as the next resume point
 *
 * Stores @c __LINE__ into @p s and emits a matching @c case label so
 * that the next LC_RESUME on the same variable jumps back to this
 * spot.  A compile-time check rejects line numbers above @c TIKU_LC_MAX
 * to keep an oversized source file from silently truncating the case
 * value.
 *
 * @warning Cannot be used inside a nested switch statement.  Each
 *          LC_SET in the same function must live on a distinct source
 *          line so that the generated case labels remain unique.
 *
 * @param s lc_t variable that receives the saved line number
 */
#define LC_SET(s)                                                          \
  do {                                                                     \
    typedef char _lc_line_overflow_[(__LINE__ <= TIKU_LC_MAX) ? 1 : -1];   \
    (void)sizeof(_lc_line_overflow_);                                      \
    (s) = __LINE__; case __LINE__:;                                        \
  } while(0)

/**
 * @def LC_END(s)
 * @brief Close the switch opened by LC_RESUME
 *
 * Every LC_RESUME must have a matching LC_END at the end of the
 * protothread body.  The @p s argument is unused but kept for
 * symmetry with LC_RESUME.
 *
 * @param s lc_t variable (ignored)
 */
#define LC_END(s) }

/*---------------------------------------------------------------------------*/
/* ADVANCED MACROS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_RESET(s)
 * @brief Restart a continuation from the beginning
 *
 * Alias for LC_INIT, provided for semantic clarity when explicitly
 * resetting an already-used continuation rather than performing a
 * first-time initialization.
 *
 * @param s lc_t variable to reset
 */
#define LC_RESET(s) LC_INIT(s)

/**
 * @def LC_IS_RESUMED(s)
 * @brief Check whether a continuation has been entered before
 *
 * Returns non-zero once LC_SET has stored a line number into @p s,
 * and zero before the first save.  Useful for one-shot init logic
 * inside a protothread that should run only on the first entry.
 *
 * @param s lc_t variable to inspect
 * @return Non-zero if a continuation point has been saved, 0 otherwise
 */
#define LC_IS_RESUMED(s) ((s) != 0)

#endif /* LC_CONF_INCLUDE */

/*---------------------------------------------------------------------------*/
/* PERSISTENT CONTINUATIONS (NVM-BACKED)                                     */
/*---------------------------------------------------------------------------*/

/*
 * Enable the persistent variant by defining TIKU_LC_PERSISTENT to 1
 * before including this header.  When enabled, an additional helper
 * API (tiku_lc_persist_*) and a parallel set of LC_*_PERSISTENT
 * macros become available for storing continuation state in
 * non-volatile memory via the kernel persist store.  The typical use
 * case is a multi-step protocol on a battery-free / intermittent
 * device where energy can disappear at any moment.
 */

#if TIKU_LC_PERSISTENT

/*---------------------------------------------------------------------------*/
/* PERSISTENT HELPER FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the persistent local-continuation store
 *
 * Recovers any LC entries that survived a power cycle by validating
 * their FRAM-backed magic numbers, and restores the internal pool's
 * next-free-slot index from the recovered entries so that fresh
 * registrations after a reboot do not collide with slots already
 * owned by recovered keys.  Safe to call multiple times -- subsequent
 * invocations are no-ops.  Must be called once at boot before any
 * tiku_lc_persist_register() call.
 */
void tiku_lc_persist_init(void);

/**
 * @brief Register a persistent LC slot under a key
 *
 * Allocates the next sizeof(lc_t)-sized chunk from an internal NVM
 * pool and binds it to @p key in the persist store.  If the key
 * already has an entry -- duplicate registration in the current boot,
 * or an entry recovered from FRAM by tiku_lc_persist_init() -- the
 * existing slot is reused without consuming a fresh pool entry, and
 * any previously stored value is preserved.
 *
 * @param key Null-terminated key string (max 7 chars + NUL)
 * @return 0 on success,
 *         -1 if the store has not been initialized,
 *         -2 if the NVM pool has no free slots,
 *         -3 if the persist store rejected the registration
 */
int tiku_lc_persist_register(const char *key);

/**
 * @brief Save a continuation value to NVM under @p key
 *
 * Writes @p val into the NVM slot bound to @p key.  Unlocks the MPU
 * internally so the caller does not need to bracket the call with
 * tiku_mpu_unlock_nvm() / tiku_mpu_lock_nvm().
 *
 * @param key Null-terminated key previously registered
 * @param val Continuation value to persist (typically a line number)
 * @return 0 on success, negative on persist-store error
 */
int tiku_lc_persist_save(const char *key, lc_t val);

/**
 * @brief Load a continuation value from NVM
 *
 * Reads the value stored under @p key.  A stored value of zero is
 * treated as "not set" and reported as an error so that
 * LC_RESUME_PERSISTENT falls through to case 0 instead of jumping
 * into the middle of a protothread body.
 *
 * @param key Null-terminated key previously registered
 * @param val Output pointer for the loaded value (untouched on error)
 * @return 0 on success,
 *         negative if the key is unknown or its stored value is zero
 */
int tiku_lc_persist_load(const char *key, lc_t *val);

/**
 * @brief Delete the NVM entry bound to @p key
 *
 * Removes @p key from the persist store entirely so the next boot
 * starts from a clean slate.  Use after a persistent protothread has
 * completed normally.  Unlocks the MPU internally.
 *
 * @param key Null-terminated key to delete
 * @return 0 on success, negative if the key is unknown
 */
int tiku_lc_persist_clear(const char *key);

/**
 * @brief Reset the NVM value to zero without deleting the entry
 *
 * Writes zero to the slot bound to @p key so LC_RESUME_PERSISTENT
 * sees an empty checkpoint and restarts from case 0, while keeping
 * the key registered for future save calls.  Unlike
 * tiku_lc_persist_clear() the slot stays bound, so subsequent
 * LC_SET_PERSISTENT writes succeed without re-registration.  Unlocks
 * the MPU internally.
 *
 * @param key Null-terminated key to reset
 * @return 0 on success, negative if the key is unknown
 */
int tiku_lc_persist_reset(const char *key);

/*---------------------------------------------------------------------------*/
/* PERSISTENT MACROS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_RESUME_PERSISTENT(s, key)
 * @brief Resume execution from an NVM-backed continuation point
 *
 * Loads the saved continuation for @p key and dispatches into the
 * protothread body.  On the first boot (or after LC_CLEAR_PERSISTENT
 * / LC_RESET_PERSISTENT) the load fails and (s) is forced to 0 so
 * execution restarts at case 0.  Without this fallback an in-memory
 * lc_t left over from a previous run could cause the protothread to
 * jump to a stale case label.
 *
 * Declares a function-scoped variable @c _tiku_lc_pkey that
 * LC_SET_PERSISTENT references, so the key only has to be specified
 * once per protothread body.
 *
 * @param s   lc_t variable that holds the in-memory continuation
 * @param key Null-terminated key previously registered
 */
#define LC_RESUME_PERSISTENT(s, key)                                       \
  const char *_tiku_lc_pkey = (key);                                       \
  {                                                                         \
    lc_t _nvm_val;                                                         \
    if (tiku_lc_persist_load(_tiku_lc_pkey, &_nvm_val) == 0) {            \
      (s) = _nvm_val;                                                      \
    } else {                                                                \
      (s) = 0;                                                              \
    }                                                                       \
  }                                                                         \
  switch(s) { case 0:

/**
 * @def LC_SET_PERSISTENT(s)
 * @brief Save the current line as a persistent resume point
 *
 * Behaves like LC_SET but also writes the new line number to NVM via
 * tiku_lc_persist_save() so the protothread can resume from this
 * point after a power loss.  Must follow a LC_RESUME_PERSISTENT in
 * the same function (which declares the @c _tiku_lc_pkey variable
 * used here).
 *
 * @param s lc_t variable that receives the saved line number
 */
#define LC_SET_PERSISTENT(s)                                               \
  do {                                                                      \
    typedef char _lc_line_overflow_[(__LINE__ <= TIKU_LC_MAX) ? 1 : -1];   \
    (void)sizeof(_lc_line_overflow_);                                      \
    (s) = __LINE__;                                                        \
    tiku_lc_persist_save(_tiku_lc_pkey, (s));                              \
    case __LINE__:;                                                        \
  } while(0)

/**
 * @def LC_CLEAR_PERSISTENT(key)
 * @brief Delete a persistent continuation entry
 *
 * Wraps tiku_lc_persist_clear() so a persistent protothread can
 * remove its NVM entry on completion, ensuring the next boot starts
 * fresh instead of resuming a finished computation.
 *
 * @param key Null-terminated key to clear
 */
#define LC_CLEAR_PERSISTENT(key) tiku_lc_persist_clear(key)

/**
 * @def LC_RESET_PERSISTENT(key)
 * @brief Reset a persistent continuation to zero, keeping the key
 *
 * Wraps tiku_lc_persist_reset() so a persistent protothread can
 * restart from the beginning without unbinding @p key.  Future
 * LC_SET_PERSISTENT calls on the same key continue to work.
 *
 * @param key Null-terminated key to reset
 */
#define LC_RESET_PERSISTENT(key) tiku_lc_persist_reset(key)

#endif /* TIKU_LC_PERSISTENT */

#endif /* TIKU_LC_H_ */
