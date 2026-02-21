/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ringbuf.c - Generic statically-allocated ring buffer implementation
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

#include "ringbuf.h"

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a ring buffer
 *
 * @param r    Pointer to the ring buffer descriptor
 * @param data Pointer to the backing byte array
 * @param size Size of the backing array in bytes
 */
void ringbuf_init(tiku_ringbuf_t *r, uint8_t *data, uint16_t size)
{
    r->data = data;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Put one byte into the ring buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @param c The byte to store
 * @return  1 on success, 0 if the buffer is full
 */
int ringbuf_put(tiku_ringbuf_t *r, uint8_t c)
{
    uint16_t next;

    next = (r->head + 1) % r->size;

    if (next == r->tail) {
        return 0;
    }

    r->data[r->head] = c;
    r->head = next;

    return 1;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Get one byte from the ring buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  The byte read, or -1 if the buffer is empty
 */
int ringbuf_get(tiku_ringbuf_t *r)
{
    uint8_t c;

    if (r->head == r->tail) {
        return -1;
    }

    c = r->data[r->tail];
    r->tail = (r->tail + 1) % r->size;

    return c;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the number of bytes stored in the buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  Number of bytes available for reading
 */
int ringbuf_elements(const tiku_ringbuf_t *r)
{
    int n;

    n = (int)r->head - (int)r->tail;
    if (n < 0) {
        n += r->size;
    }

    return n;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the number of free bytes in the buffer
 *
 * The usable capacity is (size - 1) because one slot is reserved
 * to distinguish full from empty.
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  Number of bytes that can be written before full
 */
int ringbuf_free(const tiku_ringbuf_t *r)
{
    return (r->size - 1) - ringbuf_elements(r);
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the buffer is full
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  1 if full, 0 otherwise
 */
int ringbuf_full(const tiku_ringbuf_t *r)
{
    return ((r->head + 1) % r->size) == r->tail;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the buffer is empty
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  1 if empty, 0 otherwise
 */
int ringbuf_empty(const tiku_ringbuf_t *r)
{
    return r->head == r->tail;
}
