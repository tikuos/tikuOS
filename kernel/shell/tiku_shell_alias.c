/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_alias.c - FRAM-backed shell alias table
 *
 * Implements the user-defined command shortcuts the shell parser
 * consults after a built-in command lookup misses.  An alias maps a
 * short name to a body string (which may itself chain commands with
 * ';'); the parser substitutes the body and re-dispatches it.
 *
 * Storage layout: a fixed-size array of TIKU_SHELL_ALIAS_MAX slots in
 * the .persistent (FRAM) section, paired with a 32-bit magic word for
 * first-boot detection.  Each slot holds a NUL-terminated name
 * (up to TIKU_SHELL_ALIAS_NAME_MAX chars) and a NUL-terminated body
 * (up to TIKU_SHELL_ALIAS_BODY_MAX chars); a slot is free exactly when
 * its name is empty (name[0] == '\0').  Lookup is a linear scan with
 * strcmp() name matching; there is no ordering or hashing.
 *
 * Persistence model: because the table lives in FRAM, aliases survive
 * reset, brownout, and power loss.  Every mutator (init prime, set,
 * clear) brackets its FRAM writes with tiku_mpu_unlock_nvm() /
 * tiku_mpu_lock_nvm() so the MPU keeps NVM write-protected the rest of
 * the time; read paths (lookup, get, count) touch FRAM directly but
 * never write, so they need no unlock.  The magic word distinguishes a
 * virgin or reflashed FRAM from a real persisted table: when it does
 * not match at init, every slot is emptied and the magic is stamped so
 * the prime runs exactly once per FRAM lifetime.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_alias.h"
#include <kernel/memory/tiku_mem.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* FRAM STORAGE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * Magic word guarding the persistent alias table.
 *
 * An arbitrary fixed 32-bit constant (the hex spells "ALIAS-EP").  The
 * odds of uninitialised FRAM matching it are 1 in 2^32, so a mismatch
 * at init reliably means a virgin or reflashed FRAM.  Bump this value
 * if the slot layout ever changes incompatibly, which forces a clean
 * re-prime on the next boot.
 */
#define ALIAS_MAGIC  0xA11A5E50UL   /* "ALIAS-EP", arbitrary fixed value */

/**
 * One alias table slot: a name and its expansion body.
 *
 * Each array is sized one byte larger than its MAX so a maximum-length
 * string still has room for the terminating NUL.  A slot is considered
 * free when name[0] == '\0'.
 */
typedef struct {
    char name[TIKU_SHELL_ALIAS_NAME_MAX + 1];
    char body[TIKU_SHELL_ALIAS_BODY_MAX + 1];
} alias_slot_t;

/**
 * FRAM-resident alias table (.persistent), TIKU_SHELL_ALIAS_MAX slots.
 *
 * Survives reset, brownout, and power loss.  Indexed by slot number;
 * the index passed to tiku_shell_alias_get() addresses this array
 * directly.  Mutated only under a tiku_mpu_unlock_nvm()/lock_nvm()
 * bracket; read directly without unlocking.
 */
static TIKU_DURABLE alias_slot_t alias_table[TIKU_SHELL_ALIAS_MAX];

/**
 * FRAM cell (.persistent): validity gate for alias_table.
 *
 * Holds ALIAS_MAGIC once the table has been primed.  Any other value
 * (including the all-ones / all-zeros of a fresh FRAM) triggers a
 * one-time re-init in tiku_shell_alias_init().
 */
static TIKU_DURABLE uint32_t alias_magic;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find the table index of an alias by exact name.
 *
 * Linear scan over occupied slots (name[0] != '\0') using strcmp() for
 * an exact, case-sensitive match.
 *
 * @param name  NUL-terminated alias name to look for
 * @return Slot index [0, TIKU_SHELL_ALIAS_MAX) on a hit, -1 if absent.
 */
static int
find_slot(const char *name)
{
    uint8_t i;
    for (i = 0; i < TIKU_SHELL_ALIAS_MAX; i++) {
        if (alias_table[i].name[0] != '\0' &&
            strcmp(alias_table[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Find the lowest-index free slot in the table.
 *
 * A slot is free when its name is empty (name[0] == '\0').
 *
 * @return Index of the first free slot, or -1 if all are occupied.
 */
static int
find_free_slot(void)
{
    uint8_t i;
    for (i = 0; i < TIKU_SHELL_ALIAS_MAX; i++) {
        if (alias_table[i].name[0] == '\0') {
            return (int)i;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Validate the FRAM magic word; prime the table on first boot.
 *
 * If alias_magic already equals ALIAS_MAGIC the table is a valid
 * persisted one and the call returns immediately -- so this is
 * idempotent and cheap to call on every boot.  Otherwise the FRAM is
 * virgin or was reflashed with a different magic: under a single
 * tiku_mpu_unlock_nvm()/lock_nvm() bracket, every slot's name and body
 * are emptied and the magic is stamped, so the prime runs at most once
 * per FRAM lifetime.  Call once during shell startup.
 */
void
tiku_shell_alias_init(void)
{
    uint16_t mpu_saved;
    uint8_t i;

    if (alias_magic == ALIAS_MAGIC) {
        return;
    }

    /* Virgin FRAM (or different magic from a previous build).
     * Zero every slot's name byte and stamp the magic so we
     * never re-init again. */
    mpu_saved = tiku_mpu_unlock_nvm();
    for (i = 0; i < TIKU_SHELL_ALIAS_MAX; i++) {
        alias_table[i].name[0] = '\0';
        alias_table[i].body[0] = '\0';
    }
    alias_magic = ALIAS_MAGIC;
    tiku_mpu_lock_nvm(mpu_saved);
}

/**
 * @brief Define a new alias or overwrite an existing one.
 *
 * If @p name already exists its slot is reused (body replaced);
 * otherwise the first free slot is claimed.  Both name and body are
 * copied byte by byte into the FRAM slot and NUL-terminated, under a
 * single tiku_mpu_unlock_nvm()/lock_nvm() bracket.  The change is
 * persistent immediately.
 *
 * Validation precedes any FRAM write: a NULL pointer or empty name is
 * rejected, and name/body are length-checked against
 * TIKU_SHELL_ALIAS_NAME_MAX / TIKU_SHELL_ALIAS_BODY_MAX (the trailing
 * NUL does not count toward those limits).
 *
 * @param name  Alias name (non-empty, <= TIKU_SHELL_ALIAS_NAME_MAX)
 * @param body  Expansion text (<= TIKU_SHELL_ALIAS_BODY_MAX)
 * @return TIKU_SHELL_ALIAS_OK on success;
 *         TIKU_SHELL_ALIAS_ERR_INVALID for a NULL/empty name or NULL
 *         body; TIKU_SHELL_ALIAS_ERR_TOOBIG if either string exceeds
 *         its limit; TIKU_SHELL_ALIAS_ERR_FULL if no slot is available.
 */
int
tiku_shell_alias_set(const char *name, const char *body)
{
    size_t name_len, body_len, i;
    uint16_t mpu_saved;
    int slot;

    if (name == NULL || body == NULL || name[0] == '\0') {
        return TIKU_SHELL_ALIAS_ERR_INVALID;
    }
    name_len = strlen(name);
    body_len = strlen(body);
    if (name_len > TIKU_SHELL_ALIAS_NAME_MAX ||
        body_len > TIKU_SHELL_ALIAS_BODY_MAX) {
        return TIKU_SHELL_ALIAS_ERR_TOOBIG;
    }

    slot = find_slot(name);
    if (slot < 0) {
        slot = find_free_slot();
        if (slot < 0) {
            return TIKU_SHELL_ALIAS_ERR_FULL;
        }
    }

    mpu_saved = tiku_mpu_unlock_nvm();
    for (i = 0; i < name_len; i++) {
        alias_table[slot].name[i] = name[i];
    }
    alias_table[slot].name[name_len] = '\0';
    for (i = 0; i < body_len; i++) {
        alias_table[slot].body[i] = body[i];
    }
    alias_table[slot].body[body_len] = '\0';
    tiku_mpu_lock_nvm(mpu_saved);

    return TIKU_SHELL_ALIAS_OK;
}

/**
 * @brief Remove an alias by name.
 *
 * Locates the slot via find_slot() and frees it by emptying both its
 * name and body, under a tiku_mpu_unlock_nvm()/lock_nvm() bracket so
 * the removal persists.
 *
 * @param name  Alias name to remove
 * @return TIKU_SHELL_ALIAS_OK on success;
 *         TIKU_SHELL_ALIAS_ERR_INVALID if @p name is NULL;
 *         TIKU_SHELL_ALIAS_ERR_NOTFOUND if no such alias exists.
 */
int
tiku_shell_alias_clear(const char *name)
{
    int slot;
    uint16_t mpu_saved;

    if (name == NULL) {
        return TIKU_SHELL_ALIAS_ERR_INVALID;
    }
    slot = find_slot(name);
    if (slot < 0) {
        return TIKU_SHELL_ALIAS_ERR_NOTFOUND;
    }

    mpu_saved = tiku_mpu_unlock_nvm();
    alias_table[slot].name[0] = '\0';
    alias_table[slot].body[0] = '\0';
    tiku_mpu_lock_nvm(mpu_saved);

    return TIKU_SHELL_ALIAS_OK;
}

/**
 * @brief Look up an alias body by name (the parser's hot path).
 *
 * Read-only: returns a pointer to the FRAM-resident body string, so no
 * MPU unlock is needed.  A NULL @p name is treated as "not found".
 *
 * @param name  Alias name to resolve
 * @return Pointer to the body string, or NULL if @p name is NULL or
 *         undefined.  The caller must not modify the returned string.
 */
const char *
tiku_shell_alias_lookup(const char *name)
{
    int slot = (name != NULL) ? find_slot(name) : -1;
    return (slot < 0) ? (const char *)0 : alias_table[slot].body;
}

/**
 * @brief Fetch the name and body of the alias at a given slot index.
 *
 * Enumeration helper for listing aliases.  On a populated slot it sets
 * *@p name and *@p body to the FRAM-resident strings (each output is
 * optional -- pass NULL to skip it) and returns 1.  Read-only; no MPU
 * unlock.  Note this addresses slots by raw index, so empty slots in
 * the middle of the table return 0 and must be skipped by the caller.
 *
 * @param idx   Slot index in [0, TIKU_SHELL_ALIAS_MAX)
 * @param name  Out: receives the name pointer (may be NULL to skip)
 * @param body  Out: receives the body pointer (may be NULL to skip)
 * @return 1 if the slot is populated; 0 if @p idx is out of range or
 *         the slot is empty.  Caller must not modify the strings.
 */
int
tiku_shell_alias_get(uint8_t idx, const char **name, const char **body)
{
    if (idx >= TIKU_SHELL_ALIAS_MAX) {
        return 0;
    }
    if (alias_table[idx].name[0] == '\0') {
        return 0;
    }
    if (name != NULL) {
        *name = alias_table[idx].name;
    }
    if (body != NULL) {
        *body = alias_table[idx].body;
    }
    return 1;
}

/**
 * @brief Count the currently-defined aliases.
 *
 * Linear scan counting occupied slots (name[0] != '\0').  Read-only.
 *
 * @return Number of defined aliases, 0..TIKU_SHELL_ALIAS_MAX.
 */
uint8_t
tiku_shell_alias_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < TIKU_SHELL_ALIAS_MAX; i++) {
        if (alias_table[i].name[0] != '\0') {
            n++;
        }
    }
    return n;
}
