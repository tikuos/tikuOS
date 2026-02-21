/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * hashtable.h - Generic statically-allocated hash table library
 *
 * Provides a fixed-size hash table using open addressing with linear
 * probing. Keys are unsigned 16-bit integers and values are void
 * pointers. No dynamic allocation is performed; the caller supplies
 * the backing array. Designed for small, bounded tables typical of
 * embedded systems (process IDs, resource handles, event mappings).
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

#ifndef TIKU_HASHTABLE_H_
#define TIKU_HASHTABLE_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Slot states for open-addressing
 */
enum tiku_ht_state {
    TIKU_HT_EMPTY = 0,
    TIKU_HT_OCCUPIED,
    TIKU_HT_DELETED
};

/**
 * @brief A single hash table slot
 */
typedef struct tiku_ht_entry {
    uint16_t key;
    void *value;
    uint8_t state;
} tiku_ht_entry_t;

/**
 * @brief Hash table descriptor
 *
 * Manages an open-addressed hash table with linear probing.
 * The slots array and its size are provided by the caller.
 * The count field tracks the number of occupied entries (not
 * including deleted tombstones).
 */
typedef struct tiku_hashtable {
    tiku_ht_entry_t *slots;
    uint16_t size;
    uint16_t count;
} tiku_hashtable_t;

/*---------------------------------------------------------------------------*/
/* HASH TABLE DECLARATION MACROS                                             */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_HASHTABLE(name, sz)
 * @brief Declare and allocate a statically-sized hash table
 *
 * Creates a backing slot array and a tiku_hashtable_t descriptor.
 * All slots are zero-initialized (state == TIKU_HT_EMPTY).
 * An explicit call to hashtable_init() is not required but is
 * harmless.
 *
 * @param name Name of the hash table variable
 * @param sz   Number of slots (should be somewhat larger than
 *             the expected maximum number of entries to keep
 *             load factor low)
 */
#define TIKU_HASHTABLE(name, sz)                                           \
    static tiku_ht_entry_t name##_slots[sz];                               \
    static tiku_hashtable_t name = { name##_slots, (sz), 0 }

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a hash table
 *
 * Binds the descriptor to a caller-supplied slot array, marks
 * all slots as empty, and resets the element count.
 *
 * @param ht    Pointer to the hash table descriptor
 * @param slots Pointer to the backing slot array
 * @param size  Number of slots in the array
 */
void hashtable_init(tiku_hashtable_t *ht, tiku_ht_entry_t *slots,
                    uint16_t size);

/**
 * @brief Insert or update a key-value pair
 *
 * If the key already exists its value is overwritten.
 *
 * @param ht    Pointer to the hash table descriptor
 * @param key   Key to insert
 * @param value Value to associate with the key
 * @return      1 on success, 0 if the table is full
 */
int hashtable_put(tiku_hashtable_t *ht, uint16_t key,
                  void *value);

/**
 * @brief Look up a value by key
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to search for
 * @return    Associated value, or NULL if not found
 */
void *hashtable_get(const tiku_hashtable_t *ht, uint16_t key);

/**
 * @brief Check whether a key exists in the table
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to search for
 * @return    1 if found, 0 otherwise
 */
int hashtable_has(const tiku_hashtable_t *ht, uint16_t key);

/**
 * @brief Remove a key-value pair
 *
 * The slot is marked as deleted (tombstone) to preserve
 * the linear-probing chain.
 *
 * @param ht  Pointer to the hash table descriptor
 * @param key Key to remove
 * @return    1 if removed, 0 if not found
 */
int hashtable_remove(tiku_hashtable_t *ht, uint16_t key);

/**
 * @brief Return the number of entries in the table
 *
 * @param ht Pointer to the hash table descriptor
 * @return   Current number of entries
 */
int hashtable_size(const tiku_hashtable_t *ht);

/**
 * @brief Check whether the hash table is empty
 *
 * @param ht Pointer to the hash table descriptor
 * @return   1 if empty, 0 otherwise
 */
int hashtable_empty(const tiku_hashtable_t *ht);

#endif /* TIKU_HASHTABLE_H_ */
