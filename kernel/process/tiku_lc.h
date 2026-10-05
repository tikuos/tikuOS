/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lc.h - local continuations for lightweight stackless threads.
 *
 * Captures and restores a function's execution point via case labels in a
 * switch, the substrate for protothreads.  An optional NVM-backed variant
 * survives a power cycle.  Derived from Contiki OS by Adam Dunkels.
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
 * Define it, before including this header, to a header that provides lc_t
 * and the LC_* macros; without it the switch/case implementation below is
 * used.
 */
#ifdef LC_CONF_INCLUDE
#include LC_CONF_INCLUDE
#else

/*---------------------------------------------------------------------------*/
/* DEFAULT IMPLEMENTATION: SWITCH-BASED LOCAL CONTINUATIONS                  */
/*---------------------------------------------------------------------------*/

/**
 * @typedef lc_t
 * @brief Storage type for a saved local-continuation point.
 *
 * Holds the source line to resume from.  uint16_t by default; define
 * TIKU_LC_COMPACT for uint8_t, which saves a byte per protothread, but then
 * every LC_SET must sit on file line 255 or lower.
 */
#ifdef TIKU_LC_COMPACT
typedef uint8_t  lc_t;
#define TIKU_LC_MAX 255     /**< Highest line an LC_SET may sit on */
#else
typedef uint16_t lc_t;
#define TIKU_LC_MAX 65535   /**< Highest line an LC_SET may sit on */
#endif

/*---------------------------------------------------------------------------*/
/* CORE MACROS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_INIT(s)
 * @brief Reset a local continuation to its initial state
 *
 * After init the next LC_RESUME enters at case 0 (the top of the
 * protothread body).
 *
 * @param s lc_t variable to reset
 * @note Call before the first LC_RESUME on @p s.
 */
#define LC_INIT(s) s = 0

/**
 * @def LC_RESUME(s)
 * @brief Resume execution from a previously saved continuation point
 *
 * Opens a switch statement that jumps to the case label saved by the
 * most recent LC_SET on @p s, or to case 0 on the first call.
 *
 * @note Pair it with an LC_END(s).
 * @warning The body sits inside a switch: an LC_SET inside a nested switch
 *          labels that switch, and a resume jumps past any declaration
 *          between LC_RESUME and the saved line, leaving it uninitialised.
 *
 * @param s lc_t variable holding the saved state
 */
#define LC_RESUME(s) switch(s) { case 0:

/**
 * @def LC_SET(s)
 * @brief Save the current source line as the next resume point.
 *
 * Stores __LINE__ and emits the matching case label.  An LC_SET on a line
 * above TIKU_LC_MAX fails to compile, since lc_t could not hold the line.
 *
 * @warning Cannot be used inside a nested switch.  Two LC_SETs on one source
 *          line emit the same case label twice.
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
 * @param s lc_t variable (not used)
 * @note Close every LC_RESUME with an LC_END at the end of the protothread
 *       body.
 */
#define LC_END(s) }

/*---------------------------------------------------------------------------*/
/* ADVANCED MACROS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @def LC_RESET(s)
 * @brief Restart a continuation from the beginning
 *
 * Same as LC_INIT: the next LC_RESUME enters at case 0.
 *
 * @param s lc_t variable to reset
 */
#define LC_RESET(s) LC_INIT(s)

/**
 * @def LC_IS_RESUMED(s)
 * @brief Check whether a continuation has been entered before
 *
 * Zero after LC_INIT and before the first LC_SET on @p s, non-zero once a
 * line is saved.
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
 * Defining TIKU_LC_PERSISTENT to 1 before including this header adds the
 * tiku_lc_persist_*() functions and the LC_*_PERSISTENT macros.  They keep a
 * continuation in durable memory through the kernel persist store, so a
 * protothread resumes at its last saved line after a power cycle.
 */

#if TIKU_LC_PERSISTENT

/*---------------------------------------------------------------------------*/
/* PERSISTENT HELPER FUNCTIONS                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the persistent local-continuation store.
 *
 * Recovers entries that survived a power cycle by validating their magic, and
 * restores the pool's next-free index from them so a fresh registration cannot
 * collide with a recovered slot.
 *
 * @note Call at boot, before register/save/load; later calls do nothing.
 */
void tiku_lc_persist_init(void);

/**
 * @brief Register a persistent LC slot under a key.
 *
 * Claims the next chunk of the NVM pool and binds it.  A key that already has
 * an entry, from this boot or recovered at init, keeps its slot and value.
 *
 * @param key Null-terminated key of at most TIKU_PERSIST_MAX_KEY_LEN - 1
 *            characters
 * @return 0 on success,
 *         -1 if the store has not been initialized,
 *         -2 if the NVM pool has no free slots,
 *         -3 if the persist store rejected the registration
 */
int tiku_lc_persist_register(const char *key);

/**
 * @brief Save a continuation value to NVM under @p key
 *
 * Writes @p val into the NVM slot bound to @p key, inside an MPU unlock
 * window of its own.
 *
 * @param key Null-terminated key previously registered
 * @param val Continuation value to persist (typically a line number)
 * @return 0 on success, negative on persist-store error
 */
int tiku_lc_persist_save(const char *key, lc_t val);

/**
 * @brief Load a continuation value from NVM.
 *
 * A stored zero, as tiku_lc_persist_reset() leaves, counts as not set and is
 * reported as an error.
 *
 * @param key Null-terminated key previously registered
 * @param val Output: the stored value; unchanged when the key is unknown
 *            or holds no value, 0 when the checkpoint was reset
 * @return 0 on success,
 *         negative if the key is unknown or its stored value is zero
 */
int tiku_lc_persist_load(const char *key, lc_t *val);

/**
 * @brief Delete the NVM entry bound to @p key
 *
 * Removes @p key from the persist store, inside an MPU unlock window of its
 * own; a later load of it fails.  Its pool slot is not reused this boot, nor
 * after a reboot while a live entry holds a higher slot.
 *
 * @param key Null-terminated key to delete
 * @return 0 on success, negative if the key is unknown
 */
int tiku_lc_persist_clear(const char *key);

/**
 * @brief Reset the NVM value to zero without deleting the entry.
 *
 * Stores 0, which tiku_lc_persist_load() reports as not set; the key stays
 * registered, so later saves succeed.  Opens its own MPU unlock window.
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
 * @brief Resume execution from an NVM-backed continuation point.
 *
 * Every call loads the continuation from NVM into @p s and dispatches on it;
 * a failed load sets @p s to 0, so the body starts at case 0.
 *
 * @param s   lc_t variable that holds the in-memory continuation
 * @param key Null-terminated key previously registered
 * @note Declares _tiku_lc_pkey, which LC_SET_PERSISTENT reads: use it once
 *       per function, where a declaration may stand.
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
 * @brief Save the current line as a persistent resume point.
 *
 * As LC_SET, but also writes the line to NVM so the protothread resumes there
 * after power loss.  The save's result is discarded: after a failed save the
 * next LC_RESUME_PERSISTENT resumes from what NVM still holds, or case 0.
 *
 * @param s lc_t variable that receives the saved line number
 * @note Use after LC_RESUME_PERSISTENT in the same function; it reads the key
 *       variable that macro declares.
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
 * Wraps tiku_lc_persist_clear(): the entry is deleted, and a later
 * LC_RESUME_PERSISTENT on @p key starts at case 0.
 *
 * @param key Null-terminated key to clear
 */
#define LC_CLEAR_PERSISTENT(key) tiku_lc_persist_clear(key)

/**
 * @def LC_RESET_PERSISTENT(key)
 * @brief Reset a persistent continuation to zero, keeping the key
 *
 * Wraps tiku_lc_persist_reset(): the stored line becomes 0 and @p key stays
 * registered, so later LC_SET_PERSISTENT saves succeed.
 *
 * @param key Null-terminated key to reset
 */
#define LC_RESET_PERSISTENT(key) tiku_lc_persist_reset(key)

#endif /* TIKU_LC_PERSISTENT */

#endif /* TIKU_LC_H_ */
