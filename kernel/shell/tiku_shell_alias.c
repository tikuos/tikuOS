/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_alias.c - NVM-backed shell alias table.
 *
 * A fixed array of slots in .persistent behind a magic-word gate, so aliases
 * survive power loss and a virgin store primes exactly once.  Every mutator
 * brackets its writes with the NVM unlock; read paths never write, so they do not.
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
 * An arbitrary fixed 32-bit constant, so uninitialised FRAM matches it with
 * probability 2^-32.  Bump it if the slot layout ever changes incompatibly.
 */
#define ALIAS_MAGIC  0xA11A5E50UL   /* "ALIAS-EP", arbitrary fixed value */

/**
 * One alias table slot: a name and its expansion body.
 *
 * Each array is one byte larger than its MAX so a maximum-length string still
 * has room for the terminating NUL.  A slot is free when name[0] == '\0'.
 */
typedef struct {
    char name[TIKU_SHELL_ALIAS_NAME_MAX + 1];
    char body[TIKU_SHELL_ALIAS_BODY_MAX + 1];
} alias_slot_t;

/**
 * FRAM-resident alias table (.persistent), TIKU_SHELL_ALIAS_MAX slots.
 *
 * Survives reset, brownout and power loss; indexed by slot number.  Mutated
 * only inside a tiku_mpu_unlock_nvm()/lock_nvm() bracket, read without one.
 */
static TIKU_DURABLE alias_slot_t alias_table[TIKU_SHELL_ALIAS_MAX];

/**
 * FRAM cell (.persistent): validity gate for alias_table.
 *
 * Holds ALIAS_MAGIC once the table has been primed.  Any other value, including
 * the all-ones or all-zeros of a fresh FRAM, triggers a one-time re-init.
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
 * Returns immediately when alias_magic already matches, so it is idempotent and
 * cheap on every boot.  Otherwise every slot is emptied and the magic stamped
 * inside one unlock bracket, so the prime runs at most once per FRAM lifetime.
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
     * Zero every slot's name byte and stamp the magic, so this
     * never re-inits again. */
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
 * An existing @p name reuses its slot; otherwise the first free slot is
 * claimed.  Name and body are copied into FRAM and NUL-terminated inside one
 * unlock bracket, so the change is persistent immediately.
 *
 * @note Validation precedes any FRAM write: a NULL or empty name is rejected
 *       and both strings are length-checked (the trailing NUL does not count).
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
 * The enumeration helper for listing aliases: a populated slot sets the two
 * outputs to the FRAM-resident strings and returns 1.  Read-only, no MPU
 * unlock, and addressed by raw index so the caller must skip empty slots.
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
