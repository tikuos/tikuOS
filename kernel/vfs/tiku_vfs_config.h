/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_config.h - two-bank configuration journal.
 *
 * Opt-in recoverable settings (TIKU_VFS_CONFIG=1): each change is stored with
 * a revision and a client token, applied, and applied again after a reset.
 * It is not a general transaction API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VFS_CONFIG_H_
#define TIKU_VFS_CONFIG_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Most resources one journal holds. */
#define TIKU_CFG_RESOURCES 2u
/** @brief History records kept.  For an older request tiku_cfg_lookup()
 *         returns TIKU_CFG_STALE and tiku_cfg_submit() TIKU_CFG_CONFLICT. */
#define TIKU_CFG_HISTORY 4u
/** @brief Longest normalized value, in bytes. */
#define TIKU_CFG_VALUE 32u
/** @brief Bytes in an incarnation or an operation token. */
#define TIKU_CFG_TOKEN 16u
/** @brief Size of one bank image, in bytes. */
#define TIKU_CFG_BANK_BYTES 400u

/*
 * Status codes; every failure is negative:
 *   TIKU_CFG_INVALID        bad argument, malformed request, unknown resource
 *                           or a value that does not normalize
 *   TIKU_CFG_IO             a bank read or write failed; the instance refuses
 *                           every call until it is opened again
 *   TIKU_CFG_CONFLICT       the resource is not at the expected revision, the
 *                           revision went to another token or value, or the
 *                           journal is enrolled under another incarnation
 *   TIKU_CFG_DENIED         the caller lacks the resource's capability
 *   TIKU_CFG_STALE          another incarnation, or no such history record
 *   TIKU_CFG_UNINITIALIZED  both banks blank; tiku_cfg_provision() enrolls
 *   TIKU_CFG_CORRUPT        no valid bank, banks that disagree, or a field
 *                           out of range
 *   TIKU_CFG_INCOMPATIBLE   another layout version or resource table
 *   TIKU_CFG_BUSY           a commit is in progress
 *   TIKU_CFG_EXHAUSTED      the generation or a revision would wrap
 *
 * Resource states:
 *   TIKU_CFG_UNSET          no value stored
 *   TIKU_CFG_ACCEPTED       stored, not yet applied
 *   TIKU_CFG_APPLIED        applied
 *   TIKU_CFG_RESTART        takes effect after a restart
 *   TIKU_CFG_BLOCKED        the reconcile hook refused it, or the stored value
 *                           no longer normalizes
 */
/** @brief Journal status codes. */
enum {
    TIKU_CFG_OK = 0, TIKU_CFG_INVALID = -1, TIKU_CFG_IO = -2,
    TIKU_CFG_CONFLICT = -3, TIKU_CFG_DENIED = -4, TIKU_CFG_STALE = -5,
    TIKU_CFG_UNINITIALIZED = -6, TIKU_CFG_CORRUPT = -7,
    TIKU_CFG_INCOMPATIBLE = -8, TIKU_CFG_BUSY = -9,
    TIKU_CFG_EXHAUSTED = -10
};
/** @brief Resource states, as stored and as reported in a receipt. */
enum {
    TIKU_CFG_UNSET = 0, TIKU_CFG_ACCEPTED = 1, TIKU_CFG_APPLIED = 2,
    TIKU_CFG_RESTART = 3, TIKU_CFG_BLOCKED = 4
};

/**
 * @brief Storage for the journal's two banks.
 *
 * read and write move bytes at an offset within one bank and return 0 on
 * success.  Only the journal's byte layout is stored, never C structs or
 * pointers.
 *
 * @note Each bank must be its own failure domain: a write that succeeds is
 *       ordered and durable, and a failed or torn write may damage only the
 *       bank it addressed.  A whole-region flash mirror does not meet this.
 * @note Callers serialize access to the journal in process context.
 */
typedef struct {
    void *ctx;   /**< passed back as the first argument of read and write */
    /** Read bytes from a bank; 0 on success. */
    int (*read)(void *, unsigned bank, size_t off, void *, size_t);
    /** Write bytes to a bank; 0 once they are durable. */
    int (*write)(void *, unsigned bank, size_t off, const void *, size_t);
} tiku_cfg_io_t;

/** @brief One setting the journal manages. */
typedef struct {
    uint32_t id;                     /**< nonzero, assigned; not an address */
    uint16_t schema;                 /**< nonzero version of the value format */
    uint8_t required_cap;            /**< capability bits a writer must hold */
    /** Validate and canonicalize a value into out, with no side effects;
     *  returns its length (1..TIKU_CFG_VALUE) or a negative code. */
    int (*normalize)(const char *, size_t, char out[TIKU_CFG_VALUE]);
    /**
     * @brief Apply a stored value; returns TIKU_CFG_APPLIED, TIKU_CFG_RESTART
     *        or TIKU_CFG_BLOCKED.
     * @note Only convergent setters belong here, not pulses, toggles or I/O
     *       commands: recovery calls it again, so it must re-check current
     *       policy, and a physical side effect may happen more than once.
     */
    int (*reconcile)(const char *, size_t);
} tiku_cfg_resource_t;

/** @brief A change request; its whole content identifies the operation. */
typedef struct {
    uint8_t incarnation[TIKU_CFG_TOKEN];  /**< incarnation the client read */
    uint8_t token[TIKU_CFG_TOKEN];        /**< nonzero client operation ID */
    /** Resource id, and the revision the change replaces. */
    uint32_t resource, expected_revision;
    const char *value;                    /**< value text, not normalized */
    size_t length;                        /**< bytes in value */
} tiku_cfg_request_t;

/** @brief The outcome of a request. */
typedef struct {
    uint32_t revision;                    /**< revision the request created */
    /** Resource state; duplicate is 1 when the request was already recorded
     *  and this is the original receipt. */
    uint8_t state, duplicate;
} tiku_cfg_receipt_t;

/** @brief One journal: its storage, its resources and the active bank. */
typedef struct {
    tiku_cfg_io_t io;                     /**< bank storage */
    const tiku_cfg_resource_t *resources; /**< resource table, by pointer */
    unsigned count;                       /**< entries in resources */
    int status;                           /**< TIKU_CFG_OK, or why it refuses */
    uint8_t active, busy;                 /**< bank in image; 1 in a commit */
    uint8_t image[TIKU_CFG_BANK_BYTES];   /**< copy of the active bank */
} tiku_cfg_t;

/**
 * @brief Open a journal: read both banks and adopt the newer valid one.
 *
 * Never formats storage and never applies a setting; blank storage reports
 * TIKU_CFG_UNINITIALIZED until tiku_cfg_provision() enrolls it.
 *
 * @param c          Instance to fill
 * @param io         Bank storage (copied)
 * @param resources  Up to TIKU_CFG_RESOURCES entries with unique nonzero ids
 *                   and nonzero schemas; kept by pointer
 * @param count      Entries in @p resources
 * @return TIKU_CFG_OK; TIKU_CFG_INVALID, TIKU_CFG_IO, TIKU_CFG_UNINITIALIZED,
 *         TIKU_CFG_CORRUPT or TIKU_CFG_INCOMPATIBLE, also kept in c->status
 */
int tiku_cfg_open(tiku_cfg_t *, const tiku_cfg_io_t *,
                  const tiku_cfg_resource_t *, unsigned count);
/**
 * @brief Enroll blank storage under a new incarnation.
 *
 * Repeating the call with the same incarnation returns TIKU_CFG_OK.  It is
 * not a factory reset: storage that is not blank is never formatted.
 *
 * @param c            Journal opened on blank storage
 * @param incarnation  A fresh random 128-bit value, not a boot count or UID
 * @param caller_cap   Caller's capability; must cover every resource
 * @return TIKU_CFG_OK; TIKU_CFG_INVALID, TIKU_CFG_DENIED, TIKU_CFG_BUSY,
 *         TIKU_CFG_CONFLICT for another incarnation, the open status, or a
 *         commit failure
 */
int tiku_cfg_provision(tiku_cfg_t *, const uint8_t incarnation[TIKU_CFG_TOKEN],
                       uint8_t caller_cap);
/**
 * @brief Change a resource that is still at the request's expected revision.
 *
 * Stores the normalized value with a history record, applies it through the
 * resource's reconcile hook and stores the outcome.  A retry of a request
 * still in the history returns the original receipt with duplicate set.
 *
 * @param c           Open journal
 * @param q           The request
 * @param caller_cap  Caller's capability bits
 * @param out         Receives the new revision and state
 * @return TIKU_CFG_OK; TIKU_CFG_CONFLICT, TIKU_CFG_STALE, TIKU_CFG_INVALID,
 *         TIKU_CFG_DENIED, TIKU_CFG_BUSY, TIKU_CFG_EXHAUSTED, TIKU_CFG_IO or
 *         the open status
 */
int tiku_cfg_submit(tiku_cfg_t *, const tiku_cfg_request_t *, uint8_t caller_cap,
                    tiku_cfg_receipt_t *);
/**
 * @brief Find the receipt of a request without executing it.
 *
 * Matches incarnation, resource, expected revision, token and normalized
 * value.  A request no longer in the history returns TIKU_CFG_STALE and is
 * never executed again.
 *
 * @return TIKU_CFG_OK with the receipt's duplicate set; TIKU_CFG_STALE;
 *         TIKU_CFG_CONFLICT when the revision went to another token or
 *         value; or a validation failure as for tiku_cfg_submit()
 */
int tiku_cfg_lookup(tiku_cfg_t *, const tiku_cfg_request_t *, uint8_t caller_cap,
                    tiku_cfg_receipt_t *);
/**
 * @brief Read a resource's stored value, revision and state.
 *
 * @param c         Open journal
 * @param resource  Resource id
 * @param value     Receives the value, zero-padded (no terminator when full)
 * @param length    Receives its length; 0 when no value is stored
 * @param out       Receives the revision and state
 * @return TIKU_CFG_OK, TIKU_CFG_INVALID, TIKU_CFG_BUSY or the open status
 */
int tiku_cfg_get(tiku_cfg_t *, uint32_t resource, char value[TIKU_CFG_VALUE],
                 size_t *length, tiku_cfg_receipt_t *);
/**
 * @brief Apply every stored value again and store the outcomes.
 *
 * Calls each resource's reconcile hook and commits only when a state
 * changed; the boot path runs it after tiku_cfg_open().
 *
 * @return TIKU_CFG_OK, the open status, TIKU_CFG_BUSY, TIKU_CFG_EXHAUSTED or
 *         TIKU_CFG_IO
 */
int tiku_cfg_recover(tiku_cfg_t *);
/**
 * @brief Read one retained history record, newest first; non-destructive.
 *
 * @param index  0 for the newest, up to TIKU_CFG_HISTORY - 1
 * @param value  Receives the value bytes; the rebuilt request points into it
 * @return TIKU_CFG_OK, TIKU_CFG_STALE for an empty slot, TIKU_CFG_INVALID,
 *         TIKU_CFG_BUSY or the open status
 */
int tiku_cfg_history(tiku_cfg_t *, unsigned index, tiku_cfg_request_t *,
                     char value[TIKU_CFG_VALUE], tiku_cfg_receipt_t *);
/** @brief The incarnation (TIKU_CFG_TOKEN bytes), or NULL unless open. */
const uint8_t *tiku_cfg_incarnation(const tiku_cfg_t *);
/** @brief Name of a resource state; an unknown state is "blocked". */
const char *tiku_cfg_state_name(unsigned state);
/** @brief Standard CRC-32 (zlib); seals each bank and the text requests. */
uint32_t tiku_cfg_crc32(const uint8_t *, size_t);

#endif
