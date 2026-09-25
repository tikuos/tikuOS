/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_layout.h - memory budgets a user can change, applied at the next boot.
 *
 * Each budget is a knob with a floor the system needs and a ceiling the memory
 * allows.  A request is staged, then applied before any consumer allocates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LAYOUT_H_
#define TIKU_LAYOUT_H_

#include <stddef.h>
#include <stdint.h>

#include "kernel/fs/tiku_nvm_backend.h"

/*---------------------------------------------------------------------------*/
/* KNOBS, RECORD AND RESULTS                                                 */
/*---------------------------------------------------------------------------*/

#define TIKU_LAYOUT_SCHEMA     2u
#define TIKU_LAYOUT_KNOBS_MAX  8u
#define TIKU_LAYOUT_ID_BYTES  16u

/** @brief Knob identities; stored in the record, so never renumbered. */
typedef enum {
    TIKU_KNOB_NONE     = 0,
    TIKU_KNOB_NVM_TIER = 1      /**< bytes of the region the NVM tier takes */
} tiku_layout_knob_id_t;

/** @brief What a knob's change touches. */
typedef enum {
    TIKU_LAYOUT_EFFECT_NONE  = 0,   /**< the value does not change          */
    TIKU_LAYOUT_EFFECT_BOOT  = 1,   /**< applied at next boot, no data moved */
    TIKU_LAYOUT_EFFECT_STORE = 2    /**< /data is rewritten at next boot     */
} tiku_layout_effect_t;

/** @brief How a storage rewrite treats the files that are in /data. */
typedef enum {
    TIKU_LAYOUT_METHOD_NONE  = 0,   /**< refused if the store holds files */
    TIKU_LAYOUT_METHOD_ERASE = 1    /**< the files are discarded, by request */
} tiku_layout_method_t;

/** @brief Operation phase kept in the record across a reboot. */
typedef enum {
    TIKU_LAYOUT_PHASE_NONE         = 0,
    TIKU_LAYOUT_PHASE_REWRITING    = 1, /**< authorisation consumed, not finished */
    TIKU_LAYOUT_PHASE_PROVISIONING = 2  /**< owned blank region, store unwritten */
} tiku_layout_phase_t;

/** @brief Results and receipts.  Receipts are stored, so never renumbered. */
typedef enum {
    TIKU_LAYOUT_OK        =   0,
    TIKU_LAYOUT_E_INVAL   =  -1,    /**< malformed request                    */
    TIKU_LAYOUT_E_KNOB    =  -2,    /**< unknown knob, or absent on this part */
    TIKU_LAYOUT_E_RANGE   =  -3,    /**< outside the floor or the ceiling     */
    TIKU_LAYOUT_E_STEP    =  -4,    /**< not a multiple of the knob's step    */
    TIKU_LAYOUT_E_STALE   =  -5,    /**< expected generation or revision old  */
    TIKU_LAYOUT_E_BUSY    =  -6,    /**< another request or operation is live */
    TIKU_LAYOUT_E_LOSS    =  -7,    /**< files would be lost; name the method */
    TIKU_LAYOUT_E_IO      =  -8,    /**< a record or store write failed       */
    TIKU_LAYOUT_E_PHASE   = -10,    /**< no such interrupted operation        */
    TIKU_LAYOUT_E_REUSED  = -11,    /**< the operation id names other content */
    TIKU_LAYOUT_CANCELLED = -12,    /**< receipt: the request was cancelled   */
    TIKU_LAYOUT_E_RECOVERY = -13,   /**< explicit ownership recovery required */
    TIKU_LAYOUT_E_ENTROPY = -14     /**< cannot create a fresh identity       */
} tiku_layout_err_t;

/** @brief One knob and its value, as the record stores it. */
typedef struct {
    uint8_t  id;
    uint8_t  rsv[3];
    uint32_t value;
} tiku_layout_kv_t;

/**
 * @brief The durable profile: what is applied and what is requested.
 *
 * Kept in one persist cell.  Explicit fixed-width fields; a schema other than
 * TIKU_LAYOUT_SCHEMA, or a check that does not match, reads as no record.
 */
typedef struct {
    uint16_t         schema;
    uint8_t          phase;      /**< tiku_layout_phase_t                   */
    uint8_t          method;     /**< of the pending or running operation   */
    uint32_t         contract;   /**< image the record was written under    */
    uint32_t         generation; /**< advances when an applied map changes  */
    uint32_t         revision;   /**< advances on every accepted request    */
    uint32_t         op;         /**< the pending or running operation      */
    uint32_t         src_base;   /**< rewriting: the store's old offset     */
    uint32_t         dst_base;   /**< rewriting: the store's new offset     */
    uint32_t         receipt_op; /**< the last operation that ended         */
    uint8_t          identity[TIKU_LAYOUT_ID_BYTES]; /**< recovery incarnation */
    uint32_t         request_gen; /**< original expectation, including retries */
    uint32_t         request_rev;
    uint8_t          request_n;   /**< pending[] retained for the last receipt */
    uint8_t          request_method;
    int16_t          receipt;    /**< how it ended: a tiku_layout_err_t     */
    uint8_t          n_applied;
    uint8_t          n_pending;
    tiku_layout_kv_t applied[TIKU_LAYOUT_KNOBS_MAX];
    tiku_layout_kv_t pending[TIKU_LAYOUT_KNOBS_MAX];
    uint32_t         check;      /**< FNV-1a of every field above           */
} tiku_layout_record_t;

/** @brief What boot found for /data. */
typedef enum {
    TIKU_LAYOUT_STORE_NONE      = 0, /**< no carved region on this part      */
    TIKU_LAYOUT_STORE_READY     = 1, /**< a compatible store at the base     */
    TIKU_LAYOUT_STORE_PROVISION = 2, /**< legacy value; never publish its tier */
    TIKU_LAYOUT_STORE_HELD      = 3  /**< unavailable; see the reason         */
} tiku_layout_store_t;

/** @brief Why the store is held. */
typedef enum {
    TIKU_LAYOUT_HELD_NONE = 0,
    TIKU_LAYOUT_HELD_TORN,           /**< header lost, entries remain        */
    TIKU_LAYOUT_HELD_GEOMETRY,       /**< a store for another extent         */
    TIKU_LAYOUT_HELD_VERSION,        /**< a store in another format version  */
    TIKU_LAYOUT_HELD_AMBIGUOUS,      /**< several headers, no record to pick */
    TIKU_LAYOUT_HELD_ELSEWHERE,      /**< the only header is not usable      */
    TIKU_LAYOUT_HELD_INTERRUPTED,    /**< a rewrite stopped part way         */
    TIKU_LAYOUT_HELD_IO,             /**< a write failed during boot         */
    TIKU_LAYOUT_HELD_CONTROL,        /**< missing/invalid ownership record   */
    TIKU_LAYOUT_HELD_CONTRACT,       /**< record belongs to another image    */
    TIKU_LAYOUT_HELD_REBOOT,         /**< recovered; reboot before publishing */
    TIKU_LAYOUT_HELD_ENTROPY         /**< no fresh provisioning identity      */
} tiku_layout_held_t;

/** @brief How the record read at boot. */
typedef enum {
    TIKU_LAYOUT_RECORD_ABSENT = 0,   /**< none, or a schema this build lacks */
    TIKU_LAYOUT_RECORD_VALID,
    TIKU_LAYOUT_RECORD_FOREIGN       /**< written under another image        */
} tiku_layout_record_state_t;

/** @brief The boot's findings, read by the tier, /data and the reports. */
typedef struct {
    uint32_t tier;          /**< NVM tier bytes, which is the store's base */
    uint8_t  store;         /**< tiku_layout_store_t                       */
    uint8_t  held;          /**< tiku_layout_held_t                        */
    uint8_t  record;        /**< tiku_layout_record_state_t                */
    uint8_t  corrected;     /**< reserved legacy field; always zero       */
    int16_t  outcome;       /**< result of an operation run this boot      */
    uint32_t outcome_op;    /**< its identity, 0 when none ran             */
} tiku_layout_state_t;

/** @brief A request: an operation id, the state it expects, and knobs. */
typedef struct {
    uint32_t         op;
    uint32_t         expect_gen;
    uint32_t         expect_rev;
    uint8_t          identity[TIKU_LAYOUT_ID_BYTES];
    uint8_t          method;     /**< tiku_layout_method_t */
    uint8_t          n;
    uint8_t          has_expect; /**< explicit expectation, not omitted 0:0 */
    tiku_layout_kv_t kv[TIKU_LAYOUT_KNOBS_MAX];
} tiku_layout_request_t;

/** @brief What a request would do, before anything is written. */
typedef struct {
    uint8_t  effect;        /**< the largest tiku_layout_effect_t of its knobs */
    uint8_t  bad_knob;      /**< the knob a refusal names, 0 otherwise    */
    uint16_t files;         /**< files in /data that a rewrite would lose */
    uint32_t bytes;         /**< their content bytes                      */
    uint32_t nearest;       /**< a value that passes, for RANGE and STEP  */
} tiku_layout_plan_t;

/** @brief A knob's published bounds. */
typedef struct {
    uint8_t     id;
    uint8_t     effect;     /**< tiku_layout_effect_t when it changes */
    const char *name;
    uint32_t    floor;      /**< the least the system runs with       */
    uint32_t    ceiling;    /**< the most the memory allows           */
    uint32_t    step;
    uint32_t    dflt;       /**< the value with no record             */
} tiku_layout_knob_t;

/*---------------------------------------------------------------------------*/
/* THE ENVIRONMENT: WHERE THE RECORD AND THE REGION LIVE                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief The record's storage and the region, as the service uses them.
 *
 * The production glue fills this from the persist cell and the carved region;
 * a host test fills it with doubles.  Region offsets are bytes from its base.
 */
typedef struct {
    const tiku_layout_record_t *rec;
    int  (*commit)(void *ctx, const tiku_layout_record_t *r);
    void  *commit_ctx;
    tiku_nvm_backend_t region;  /**< base and size; write is not used     */
    int  (*write)(void *ctx, size_t off, const void *src, size_t len);
    void  *write_ctx;
    uint32_t default_tier;
    uint32_t step;
    int (*random)(void *ctx, uint8_t *out, size_t len); /**< provision/recovery */
    void *random_ctx;
    /* The durable image as last persisted, searched only when the record in
     * its fixed place does not validate: an image from before the place was
     * fixed left it wherever its own durable variables did.  NULL: none. */
    const uint8_t *durable;
    size_t         durable_len;
} tiku_layout_env_t;

/*---------------------------------------------------------------------------*/
/* SERVICE, ON AN EXPLICIT ENVIRONMENT                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Decide the split and the store's state; run a staged operation.
 *
 * Blank media gets a store and checked ownership before its tier is published.
 * Entropy/write failures and unowned nonblank regions expose no NVM tier.
 * Existing image-compatible ownership needs no boot-time entropy.
 */
int tiku_layout_boot_env(const tiku_layout_env_t *env, tiku_layout_state_t *st);

/** @brief Validate a request and say what it would do; writes nothing. */
int tiku_layout_plan_env(const tiku_layout_env_t *env,
                         const tiku_layout_request_t *req,
                         tiku_layout_plan_t *plan);

/** @brief Validate and record a request for the next boot. */
int tiku_layout_stage_env(const tiku_layout_env_t *env,
                          const tiku_layout_request_t *req,
                          tiku_layout_plan_t *plan);

/** @brief Drop the pending request @p op before it runs. */
int tiku_layout_cancel_env(const tiku_layout_env_t *env, uint32_t op);

/**
 * @brief Finish the interrupted operation @p op the way it was authorised.
 *
 * Refused unless the record shows @p op interrupted; never runs twice.
 */
int tiku_layout_resume_env(const tiku_layout_env_t *env,
                           tiku_layout_state_t *st, uint32_t op);

/**
 * @brief Explicitly accept a compatible store at a user-selected offset.
 *
 * Never formats or scans for ownership. Creates a fresh random identity;
 * unavailable entropy or a failed commit leaves the store held. Only allowed
 * while boot has withheld the tier. Reboot is required before normal use.
 */
int tiku_layout_recover_env(const tiku_layout_env_t *env,
                            tiku_layout_state_t *st, uint32_t base);

/** @brief Knob @p i's bounds on this environment; 0 past the last knob. */
int tiku_layout_knob_env(const tiku_layout_env_t *env, unsigned i,
                         tiku_layout_knob_t *out);

/** @brief The value of knob @p id among @p n entries of @p kv, or @p dflt. */
uint32_t tiku_layout_kv_get(const tiku_layout_kv_t *kv, uint8_t n,
                            uint8_t id, uint32_t dflt);

/** @brief Set @p r's check over its other fields, as a commit does. */
void tiku_layout_seal(tiku_layout_record_t *r);

/** @brief Parse key=value controls, including identity=HEX32. @return 0/-1. */
int tiku_layout_parse(const char *text, tiku_layout_request_t *req);

/** @brief A short word for a result, a store state or a held reason. */
const char *tiku_layout_err_name(int err);
const char *tiku_layout_store_name(uint8_t store);
const char *tiku_layout_held_name(uint8_t held);

/*---------------------------------------------------------------------------*/
/* SERVICE, ON THIS BOARD                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Run the boot decision once; later calls return at once. */
void tiku_layout_boot(void);

/** @brief The boot's findings (runs the boot first if it has not run). */
const tiku_layout_state_t *tiku_layout_state(void);

/** @brief The record as it stands. */
const tiku_layout_record_t *tiku_layout_record(void);

/** @brief 1 when the record holds a valid profile, else 0. */
int tiku_layout_have_record(void);

/** @brief This board's environment, for the shell and the VFS nodes. */
const tiku_layout_env_t *tiku_layout_env(void);

/** @brief Store offset for binding/explicit format; not an allocatable tier size. */
uint32_t tiku_layout_base(void);

/**
 * @brief Take the store just created at @p base as the applied one.
 *
 * Called after an explicit format. Keeps an existing compatible identity;
 * otherwise performs explicit recovery and withholds the tier until reboot.
 * @return TIKU_LAYOUT_OK, or a negative result (including entropy/write failure).
 */
int tiku_layout_adopt(uint32_t base);

/** @brief Resume the interrupted operation @p op on this board. */
int tiku_layout_resume(uint32_t op);
int tiku_layout_recover(uint32_t base);

/** @brief Render an identity into a 33-byte lowercase hexadecimal string. */
void tiku_layout_identity_text(const uint8_t id[TIKU_LAYOUT_ID_BYTES], char out[33]);

#endif /* TIKU_LAYOUT_H_ */
