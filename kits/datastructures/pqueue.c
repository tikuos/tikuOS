/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * pqueue.c - Generic statically-allocated priority queue implementation
 *
 * Implements a binary min-heap. The element with the lowest numeric
 * priority value sits at the root and is returned by pqueue_remove().
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

#include "pqueue.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Swap two heap entries
 */
static void pqueue_swap(tiku_pqueue_entry_t *a,
                        tiku_pqueue_entry_t *b)
{
    tiku_pqueue_entry_t tmp;

    tmp = *a;
    *a = *b;
    *b = tmp;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Restore heap property upward from index i
 */
static void pqueue_sift_up(tiku_pqueue_t *pq, uint16_t i)
{
    uint16_t parent;

    while (i > 0) {
        parent = (i - 1) / 2;
        if (pq->heap[parent].priority <= pq->heap[i].priority) {
            break;
        }
        pqueue_swap(&pq->heap[parent], &pq->heap[i]);
        i = parent;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Restore heap property downward from index i
 */
static void pqueue_sift_down(tiku_pqueue_t *pq, uint16_t i)
{
    uint16_t left, right, smallest;

    for (;;) {
        left = 2 * i + 1;
        right = 2 * i + 2;
        smallest = i;

        if (left < pq->count &&
            pq->heap[left].priority <
                pq->heap[smallest].priority) {
            smallest = left;
        }
        if (right < pq->count &&
            pq->heap[right].priority <
                pq->heap[smallest].priority) {
            smallest = right;
        }
        if (smallest == i) {
            break;
        }
        pqueue_swap(&pq->heap[i], &pq->heap[smallest]);
        i = smallest;
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a priority queue
 *
 * @param pq      Pointer to the priority queue descriptor
 * @param heap    Pointer to the backing entry array
 * @param maxsize Maximum number of entries the array can hold
 */
void pqueue_init(tiku_pqueue_t *pq, tiku_pqueue_entry_t *heap,
                 uint16_t maxsize)
{
    pq->heap = heap;
    pq->maxsize = maxsize;
    pq->count = 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Insert an element into the priority queue
 *
 * @param pq       Pointer to the priority queue descriptor
 * @param data     Opaque data pointer to store
 * @param priority Priority value (lower = higher priority)
 * @return         1 on success, 0 if the queue is full
 */
int pqueue_insert(tiku_pqueue_t *pq, void *data,
                  uint16_t priority)
{
    if (pq->count >= pq->maxsize) {
        return 0;
    }

    pq->heap[pq->count].priority = priority;
    pq->heap[pq->count].data = data;
    pqueue_sift_up(pq, pq->count);
    pq->count++;

    return 1;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Remove and return the highest-priority element
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Data pointer of the removed element, or NULL if empty
 */
void *pqueue_remove(tiku_pqueue_t *pq)
{
    void *data;

    if (pq->count == 0) {
        return NULL;
    }

    data = pq->heap[0].data;
    pq->count--;

    if (pq->count > 0) {
        pq->heap[0] = pq->heap[pq->count];
        pqueue_sift_down(pq, 0);
    }

    return data;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Peek at the highest-priority element without removing it
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Data pointer of the top element, or NULL if empty
 */
void *pqueue_peek(const tiku_pqueue_t *pq)
{
    if (pq->count == 0) {
        return NULL;
    }

    return pq->heap[0].data;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the priority of the top element
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Priority of the top element, or UINT16_MAX if empty
 */
uint16_t pqueue_peek_priority(const tiku_pqueue_t *pq)
{
    if (pq->count == 0) {
        return UINT16_MAX;
    }

    return pq->heap[0].priority;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the priority queue is empty
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   1 if empty, 0 otherwise
 */
int pqueue_empty(const tiku_pqueue_t *pq)
{
    return pq->count == 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the priority queue is full
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   1 if full, 0 otherwise
 */
int pqueue_full(const tiku_pqueue_t *pq)
{
    return pq->count >= pq->maxsize;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the number of elements in the priority queue
 *
 * @param pq Pointer to the priority queue descriptor
 * @return   Current number of elements
 */
int pqueue_size(const tiku_pqueue_t *pq)
{
    return (int)pq->count;
}
