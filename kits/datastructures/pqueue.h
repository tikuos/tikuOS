/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * pqueue.h - Generic statically-allocated priority queue library
 *
 * Provides a fixed-size min-heap priority queue. Each entry holds a
 * numeric priority and a void-pointer payload. The element with the
 * lowest priority value is dequeued first (min-priority). The caller
 * supplies the backing array; no dynamic allocation is performed.
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

#ifndef TIKU_PQUEUE_H_
#define TIKU_PQUEUE_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief A single priority-queue entry
 *
 * Pairs a numeric priority with an opaque data pointer. Lower
 * priority values are dequeued first (min-heap).
 */
typedef struct tiku_pqueue_entry {
    uint16_t priority;
    void *data;
} tiku_pqueue_entry_t;

/**
 * @brief Priority queue descriptor
 *
 * Manages a binary min-heap stored in a caller-supplied array of
 * tiku_pqueue_entry_t. The count field tracks the current number
 * of entries; maxsize is the capacity of the backing array.
 */
typedef struct tiku_pqueue {
    tiku_pqueue_entry_t *heap;
    uint16_t maxsize;
    uint16_t count;
} tiku_pqueue_t;

/*---------------------------------------------------------------------------*/
/* PRIORITY QUEUE DECLARATION MACROS                                         */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_PQUEUE(name, sz)
 * @brief Declare and allocate a statically-sized priority queue
 *
 * Creates a backing array and a tiku_pqueue_t descriptor.
 * The queue is ready for use immediately.
 *
 * @param name Name of the priority queue variable
 * @param sz   Maximum number of entries
 */
#define TIKU_PQUEUE(name, sz)                                              \
    static tiku_pqueue_entry_t name##_heap[sz];                            \
    static tiku_pqueue_t name = { name##_heap, (sz), 0 }

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a priority queue
 *
 * Binds the descriptor to a caller-supplied entry array and
 * resets the element count to zero.
 *
 * @param pq      Pointer to the priority queue descriptor
 * @param heap    Pointer to the backing entry array
 * @param maxsize Maximum number of entries the array can hold
 */
void pqueue_init(tiku_pqueue_t *pq, tiku_pqueue_entry_t *heap,
                 uint16_t maxsize);

/**
 * @brief Insert an element into the priority queue
 *
 * @param pq       Pointer to the priority queue descriptor
 * @param data     Opaque data pointer to store
 * @param priority Priority value (lower = higher priority)
 * @return         1 on success, 0 if the queue is full
 */
int pqueue_insert(tiku_pqueue_t *pq, void *data,
                  uint16_t priority);

/**
 * @brief Remove and return the highest-priority element
 *
 * The element with the lowest numeric priority value is removed.
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Data pointer of the removed element, or NULL if empty
 */
void *pqueue_remove(tiku_pqueue_t *pq);

/**
 * @brief Peek at the highest-priority element without removing it
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Data pointer of the top element, or NULL if empty
 */
void *pqueue_peek(const tiku_pqueue_t *pq);

/**
 * @brief Return the priority of the top element
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Priority of the top element, or UINT16_MAX if empty
 */
uint16_t pqueue_peek_priority(const tiku_pqueue_t *pq);

/**
 * @brief Check whether the priority queue is empty
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   1 if empty, 0 otherwise
 */
int pqueue_empty(const tiku_pqueue_t *pq);

/**
 * @brief Check whether the priority queue is full
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   1 if full, 0 otherwise
 */
int pqueue_full(const tiku_pqueue_t *pq);

/**
 * @brief Return the number of elements in the priority queue
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Current number of elements
 */
int pqueue_size(const tiku_pqueue_t *pq);

#endif /* TIKU_PQUEUE_H_ */
