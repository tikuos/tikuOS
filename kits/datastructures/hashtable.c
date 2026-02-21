/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * hashtable.c - Generic statically-allocated hash table implementation
 *
 * Uses open addressing with linear probing. Deleted slots are
 * marked with a tombstone so that probe chains remain intact.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "hashtable.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Simple 16-bit hash function
 *
 * Multiplies by a prime and folds the result into the table size.
 * Adequate for small, bounded tables on 16-bit targets.
 */
static uint16_t hashtable_hash(uint16_t key, uint16_t size)
{
    return (uint16_t)((key * 37u) % size);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a hash table
 *
 * @param ht    Pointer to the hash table descriptor
 * @param slots Pointer to the backing slot array
 * @param size  Number of slots in the array
 */
void hashtable_init(tiku_hashtable_t *ht, tiku_ht_entry_t *slots,
                    uint16_t size)
{
    uint16_t i;

    ht->slots = slots;
    ht->size = size;
    ht->count = 0;

    for (i = 0; i < size; i++) {
        slots[i].state = TIKU_HT_EMPTY;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Insert or update a key-value pair
 *
 * Scans linearly from the hash index. If an existing entry with
 * the same key is found its value is overwritten. Otherwise the
 * first empty or deleted slot is used for insertion.
 *
 * @param ht    Pointer to the hash table descriptor
 * @param key   Key to insert
 * @param value Value to associate with the key
 * @return      1 on success, 0 if the table is full
 */
int hashtable_put(tiku_hashtable_t *ht, uint16_t key,
                  void *value)
{
    uint16_t idx, start, first_deleted;
    int found_deleted;

    if (ht->count >= ht->size) {
        return 0;
    }

    start = hashtable_hash(key, ht->size);
    idx = start;
    found_deleted = 0;
    first_deleted = 0;

    do {
        if (ht->slots[idx].state == TIKU_HT_EMPTY) {
            /* Use the first deleted slot if we passed one */
            if (found_deleted) {
                idx = first_deleted;
            }
            ht->slots[idx].key = key;
            ht->slots[idx].value = value;
            ht->slots[idx].state = TIKU_HT_OCCUPIED;
            ht->count++;
            return 1;
        }

        if (ht->slots[idx].state == TIKU_HT_DELETED) {
            if (!found_deleted) {
                found_deleted = 1;
                first_deleted = idx;
            }
        } else if (ht->slots[idx].key == key) {
            /* Key already present; update value */
            ht->slots[idx].value = value;
            return 1;
        }

        idx = (idx + 1) % ht->size;
    } while (idx != start);

    /* Table is full of occupied/deleted; use deleted slot */
    if (found_deleted) {
        ht->slots[first_deleted].key = key;
        ht->slots[first_deleted].value = value;
        ht->slots[first_deleted].state = TIKU_HT_OCCUPIED;
        ht->count++;
        return 1;
    }

    return 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Look up a value by key
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to search for
 * @return    Associated value, or NULL if not found
 */
void *hashtable_get(const tiku_hashtable_t *ht, uint16_t key)
{
    uint16_t idx, start;

    start = hashtable_hash(key, ht->size);
    idx = start;

    do {
        if (ht->slots[idx].state == TIKU_HT_EMPTY) {
            return NULL;
        }
        if (ht->slots[idx].state == TIKU_HT_OCCUPIED &&
            ht->slots[idx].key == key) {
            return ht->slots[idx].value;
        }
        idx = (idx + 1) % ht->size;
    } while (idx != start);

    return NULL;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether a key exists in the table
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to search for
 * @return    1 if found, 0 otherwise
 */
int hashtable_has(const tiku_hashtable_t *ht, uint16_t key)
{
    uint16_t idx, start;

    start = hashtable_hash(key, ht->size);
    idx = start;

    do {
        if (ht->slots[idx].state == TIKU_HT_EMPTY) {
            return 0;
        }
        if (ht->slots[idx].state == TIKU_HT_OCCUPIED &&
            ht->slots[idx].key == key) {
            return 1;
        }
        idx = (idx + 1) % ht->size;
    } while (idx != start);

    return 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Remove a key-value pair
 *
 * Marks the slot as deleted (tombstone) rather than empty so
 * that linear-probing chains past this slot remain reachable.
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to remove
 * @return    1 if removed, 0 if not found
 */
int hashtable_remove(tiku_hashtable_t *ht, uint16_t key)
{
    uint16_t idx, start;

    start = hashtable_hash(key, ht->size);
    idx = start;

    do {
        if (ht->slots[idx].state == TIKU_HT_EMPTY) {
            return 0;
        }
        if (ht->slots[idx].state == TIKU_HT_OCCUPIED &&
            ht->slots[idx].key == key) {
            ht->slots[idx].state = TIKU_HT_DELETED;
            ht->count--;
            return 1;
        }
        idx = (idx + 1) % ht->size;
    } while (idx != start);

    return 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the number of entries in the table
 *
 * @param ht Pointer to the hash table descriptor
 * @return   Current number of entries
 */
int hashtable_size(const tiku_hashtable_t *ht)
{
    return (int)ht->count;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the hash table is empty
 *
 * @param ht Pointer to the hash table descriptor
 * @return   1 if empty, 0 otherwise
 */
int hashtable_empty(const tiku_hashtable_t *ht)
{
    return ht->count == 0;
}
