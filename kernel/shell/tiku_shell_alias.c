/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_alias.c - FRAM-backed shell alias table
 *
 * Storage layout: a fixed-size array in .persistent, paired
 * with a magic word for first-boot detection. Each slot holds
 * a NUL-terminated name and a NUL-terminated body; an empty
 * name (name[0] == '\0') means the slot is free.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_alias.h"
#include <kernel/memory/tiku_mem.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* FRAM STORAGE                                                              */
/*---------------------------------------------------------------------------*/

#define ALIAS_MAGIC  0xA11A5E50UL   /* "ALIAS-EP", arbitrary fixed value */

typedef struct {
    char name[TIKU_SHELL_ALIAS_NAME_MAX + 1];
    char body[TIKU_SHELL_ALIAS_BODY_MAX + 1];
} alias_slot_t;

static alias_slot_t __attribute__((section(".persistent")))
    alias_table[TIKU_SHELL_ALIAS_MAX];

static uint32_t __attribute__((section(".persistent")))
    alias_magic;

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/* Find slot index for an exact name match. Returns -1 if absent. */
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

/* Find first empty slot. Returns -1 if all are occupied. */
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

const char *
tiku_shell_alias_lookup(const char *name)
{
    int slot = (name != NULL) ? find_slot(name) : -1;
    return (slot < 0) ? (const char *)0 : alias_table[slot].body;
}

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
