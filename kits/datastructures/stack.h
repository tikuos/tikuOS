/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * stack.h - Generic statically-allocated stack library
 *
 * Provides a fixed-size, LIFO stack of void pointers. The caller
 * supplies the backing array; the library manages the top-of-stack
 * index. Suitable for interrupt-safe usage on embedded targets
 * when accessed from a single context or with interrupts disabled.
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

#ifndef TIKU_STACK_H_
#define TIKU_STACK_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stack descriptor
 *
 * Holds the state of a single stack instance. The caller provides
 * a statically-allocated array of void pointers via stack_init().
 * The top field is the index of the next free slot; it equals zero
 * when the stack is empty and maxsize when the stack is full.
 */
typedef struct tiku_stack {
    void **data;
    uint16_t maxsize;
    uint16_t top;
} tiku_stack_t;

/*---------------------------------------------------------------------------*/
/* STACK DECLARATION MACROS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @def TIKU_STACK(name, sz)
 * @brief Declare and allocate a statically-sized stack
 *
 * Creates a backing array of void pointers and a tiku_stack_t
 * descriptor. The stack is ready for use immediately; an explicit
 * call to stack_init() is not required but is harmless.
 *
 * @param name Name of the stack variable
 * @param sz   Maximum number of elements
 */
#define TIKU_STACK(name, sz)                                               \
    static void *name##_data[sz];                                          \
    static tiku_stack_t name = { name##_data, (sz), 0 }

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a stack
 *
 * Binds the descriptor to a caller-supplied array and resets
 * the top index to zero.
 *
 * @param s       Pointer to the stack descriptor
 * @param data    Pointer to the backing void-pointer array
 * @param maxsize Maximum number of elements the array can hold
 */
void stack_init(tiku_stack_t *s, void **data, uint16_t maxsize);

/**
 * @brief Push an item onto the stack
 *
 * @param s    Pointer to the stack descriptor
 * @param item Pointer to push
 * @return     1 on success, 0 if the stack is full
 */
int stack_push(tiku_stack_t *s, void *item);

/**
 * @brief Pop the top item from the stack
 *
 * @param s Pointer to the stack descriptor
 * @return  The popped pointer, or NULL if the stack is empty
 */
void *stack_pop(tiku_stack_t *s);

/**
 * @brief Peek at the top item without removing it
 *
 * @param s Pointer to the stack descriptor
 * @return  The top pointer, or NULL if the stack is empty
 */
void *stack_peek(const tiku_stack_t *s);

/**
 * @brief Check whether the stack is empty
 *
 * @param s Pointer to the stack descriptor
 * @return  1 if empty, 0 otherwise
 */
int stack_empty(const tiku_stack_t *s);

/**
 * @brief Check whether the stack is full
 *
 * @param s Pointer to the stack descriptor
 * @return  1 if full, 0 otherwise
 */
int stack_full(const tiku_stack_t *s);

/**
 * @brief Return the number of elements on the stack
 *
 * @param s Pointer to the stack descriptor
 * @return  Current number of elements
 */
int stack_size(const tiku_stack_t *s);

#endif /* TIKU_STACK_H_ */
