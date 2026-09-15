/* TikuOS -- opt-in recoverable configuration, not a general transaction API.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_VFS_CONFIG_H_
#define TIKU_VFS_CONFIG_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_CFG_RESOURCES 2u
#define TIKU_CFG_HISTORY 4u
#define TIKU_CFG_VALUE 32u
#define TIKU_CFG_TOKEN 16u
#define TIKU_CFG_BANK_BYTES 400u

enum {
    TIKU_CFG_OK = 0, TIKU_CFG_INVALID = -1, TIKU_CFG_IO = -2,
    TIKU_CFG_CONFLICT = -3, TIKU_CFG_DENIED = -4, TIKU_CFG_STALE = -5,
    TIKU_CFG_UNINITIALIZED = -6, TIKU_CFG_CORRUPT = -7,
    TIKU_CFG_INCOMPATIBLE = -8, TIKU_CFG_BUSY = -9,
    TIKU_CFG_EXHAUSTED = -10
};
enum {
    TIKU_CFG_UNSET = 0, TIKU_CFG_ACCEPTED = 1, TIKU_CFG_APPLIED = 2,
    TIKU_CFG_RESTART = 3, TIKU_CFG_BLOCKED = 4
};

/* Both banks must occupy INDEPENDENT failure domains. A successful write is
 * ordered and durable; a failed/torn write may affect only its addressed bank.
 * A whole-region flash mirror does NOT satisfy this contract. No native C
 * structs or pointers are stored. Callers serialize access in process context. */
typedef struct {
    void *ctx;
    int (*read)(void *, unsigned bank, size_t off, void *, size_t);
    int (*write)(void *, unsigned bank, size_t off, const void *, size_t);
} tiku_cfg_io_t;

typedef struct {
    uint32_t id;                     /* explicitly assigned, never an address */
    uint16_t schema;
    uint8_t required_cap;
    /* Pure validation/canonicalization. Return byte count 1..VALUE or <0. */
    int (*normalize)(const char *, size_t, char out[TIKU_CFG_VALUE]);
    /* Only convergent configuration setters belong here. No pulse/toggle/I/O
     * commands. Re-check current policy on recovery. Return APPLIED, RESTART,
     * or BLOCKED; this does not imply exactly-once physical side effects. */
    int (*reconcile)(const char *, size_t);
} tiku_cfg_resource_t;

typedef struct {
    uint8_t incarnation[TIKU_CFG_TOKEN];
    uint8_t token[TIKU_CFG_TOKEN];    /* nonzero client-generated operation ID */
    uint32_t resource, expected_revision;
    const char *value;
    size_t length;
} tiku_cfg_request_t;

typedef struct {
    uint32_t revision;
    uint8_t state, duplicate;
} tiku_cfg_receipt_t;

typedef struct {
    tiku_cfg_io_t io;
    const tiku_cfg_resource_t *resources;
    unsigned count;
    int status;
    uint8_t active, busy;
    uint8_t image[TIKU_CFG_BANK_BYTES];
} tiku_cfg_t;

/* Mount never formats corrupt/unknown storage and never applies hardware. */
int tiku_cfg_open(tiku_cfg_t *, const tiku_cfg_io_t *,
                  const tiku_cfg_resource_t *, unsigned count);
/* Explicit enrollment on virgin storage ONLY. Use a fresh random 128-bit
 * incarnation, not a boot counter/device UID. Retrying format with the same
 * incarnation is harmless. This is not a factory-reset interface. */
int tiku_cfg_provision(tiku_cfg_t *, const uint8_t incarnation[TIKU_CFG_TOKEN],
                       uint8_t caller_cap);
int tiku_cfg_submit(tiku_cfg_t *, const tiku_cfg_request_t *, uint8_t caller_cap,
                    tiku_cfg_receipt_t *);
/* Lookup is bound to incarnation, resource, expected revision, token AND
 * canonical payload. Once history expires it returns STALE, never re-executes. */
int tiku_cfg_lookup(tiku_cfg_t *, const tiku_cfg_request_t *, uint8_t caller_cap,
                    tiku_cfg_receipt_t *);
int tiku_cfg_get(tiku_cfg_t *, uint32_t resource, char value[TIKU_CFG_VALUE],
                 size_t *length, tiku_cfg_receipt_t *);
int tiku_cfg_recover(tiku_cfg_t *);
/* Read one retained receipt, newest first; no destructive queue consumption. */
int tiku_cfg_history(tiku_cfg_t *, unsigned index, tiku_cfg_request_t *,
                     char value[TIKU_CFG_VALUE], tiku_cfg_receipt_t *);
const uint8_t *tiku_cfg_incarnation(const tiku_cfg_t *);
const char *tiku_cfg_state_name(unsigned state);
/* Standard CRC32 for persisted records and the text transport envelope. */
uint32_t tiku_cfg_crc32(const uint8_t *, size_t);

#endif
