/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * stack.c - Generic statically-allocated stack implementation
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

#include "stack.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a stack
 *
 * @param s       Pointer to the stack descriptor
 * @param data    Pointer to the backing void-pointer array
 * @param maxsize Maximum number of elements the array can hold
 */
void stack_init(tiku_stack_t *s, void **data, uint16_t maxsize)
{
    s->data = data;
    s->maxsize = maxsize;
    s->top = 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Push an item onto the stack
 *
 * @param s    Pointer to the stack descriptor
 * @param item Pointer to push
 * @return     1 on success, 0 if the stack is full
 */
int stack_push(tiku_stack_t *s, void *item)
{
    if (s->top >= s->maxsize) {
        return 0;
    }

    s->data[s->top] = item;
    s->top++;

    return 1;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Pop the top item from the stack
 *
 * @param s Pointer to the stack descriptor
 * @return  The popped pointer, or NULL if the stack is empty
 */
void *stack_pop(tiku_stack_t *s)
{
    if (s->top == 0) {
        return NULL;
    }

    s->top--;
    return s->data[s->top];
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Peek at the top item without removing it
 *
 * @param s Pointer to the stack descriptor
 * @return  The top pointer, or NULL if the stack is empty
 */
void *stack_peek(const tiku_stack_t *s)
{
    if (s->top == 0) {
        return NULL;
    }

    return s->data[s->top - 1];
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the stack is empty
 *
 * @param s Pointer to the stack descriptor
 * @return  1 if empty, 0 otherwise
 */
int stack_empty(const tiku_stack_t *s)
{
    return s->top == 0;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Check whether the stack is full
 *
 * @param s Pointer to the stack descriptor
 * @return  1 if full, 0 otherwise
 */
int stack_full(const tiku_stack_t *s)
{
    return s->top >= s->maxsize;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Return the number of elements on the stack
 *
 * @param s Pointer to the stack descriptor
 * @return  Current number of elements
 */
int stack_size(const tiku_stack_t *s)
{
    return (int)s->top;
}
