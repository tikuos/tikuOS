/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ringbuf.h - Generic statically-allocated ring buffer library
 *
 * Provides a fixed-size, byte-oriented circular buffer suitable for
 * interrupt-safe producer/consumer patterns on embedded targets.
 * The caller supplies the backing array; the library manages head,
 * tail, and occupancy tracking. The buffer can hold at most
 * (size - 1) bytes so that full and empty states are distinguishable.
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

#ifndef TIKU_RINGBUF_H_
#define TIKU_RINGBUF_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Ring buffer descriptor
 *
 * Holds the state of a single ring buffer instance. The caller
 * provides a statically-allocated byte array via ringbuf_init();
 * this struct tracks the head (write) and tail (read) indices
 * plus the total size of the backing array.
 *
 * The buffer can store at most (size - 1) bytes so that the
 * head == tail condition unambiguously means "empty".
 */
typedef struct tiku_ringbuf {
    uint8_t *data;
    uint16_t size;
    uint16_t head;
    uint16_t tail;
} tiku_ringbuf_t;

/*---------------------------------------------------------------------------*/
/* RING BUFFER DECLARATION MACROS                                            */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_RINGBUF(name, size)
 * @brief Declare and allocate a statically-sized ring buffer
 *
 * Creates a backing array and a tiku_ringbuf_t descriptor.
 * The buffer must still be initialized with ringbuf_init()
 * before first use.
 *
 * @param name Name of the ring buffer variable
 * @param sz   Total size of the backing array in bytes
 */
#define TIKU_RINGBUF(name, sz)                                             \
    static uint8_t name##_data[sz];                                        \
    static tiku_ringbuf_t name = { name##_data, sz, 0, 0 }

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a ring buffer
 *
 * Binds the descriptor to a caller-supplied byte array and
 * resets the head and tail indices to zero.
 *
 * @param r    Pointer to the ring buffer descriptor
 * @param data Pointer to the backing byte array
 * @param size Size of the backing array in bytes
 */
void ringbuf_init(tiku_ringbuf_t *r, uint8_t *data, uint16_t size);

/**
 * @brief Put one byte into the ring buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @param c The byte to store
 * @return  1 on success, 0 if the buffer is full
 */
int ringbuf_put(tiku_ringbuf_t *r, uint8_t c);

/**
 * @brief Get one byte from the ring buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  The byte read, or -1 if the buffer is empty
 */
int ringbuf_get(tiku_ringbuf_t *r);

/**
 * @brief Return the number of bytes stored in the buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  Number of bytes available for reading
 */
int ringbuf_elements(const tiku_ringbuf_t *r);

/**
 * @brief Return the number of free bytes in the buffer
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  Number of bytes that can be written before full
 */
int ringbuf_free(const tiku_ringbuf_t *r);

/**
 * @brief Check whether the buffer is full
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  1 if full, 0 otherwise
 */
int ringbuf_full(const tiku_ringbuf_t *r);

/**
 * @brief Check whether the buffer is empty
 *
 * @param r Pointer to the ring buffer descriptor
 * @return  1 if empty, 0 otherwise
 */
int ringbuf_empty(const tiku_ringbuf_t *r);

#endif /* TIKU_RINGBUF_H_ */
