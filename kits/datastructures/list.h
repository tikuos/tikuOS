/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * list.h - Generic singly-linked list library
 *
 * Provides a set of functions for manipulating intrusive singly-linked
 * lists. A list element is any struct whose first member is a pointer
 * (used by the library to form the chain). Lists are declared with the
 * TIKU_LIST() macro and manipulated through the list_*() API.
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

#ifndef TIKU_LIST_H_
#define TIKU_LIST_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Opaque list handle
 *
 * A list handle is a pointer-to-pointer that refers to the head of the
 * list. The double indirection allows the library to update the head
 * pointer when items are added or removed.
 */
typedef void **tiku_list_t;

/*---------------------------------------------------------------------------*/
/* LIST DECLARATION MACROS                                                   */
/*---------------------------------------------------------------------------*/

/** @cond INTERNAL */
#define TIKU_LIST_CONCAT2(s1, s2)   s1##s2
#define TIKU_LIST_CONCAT(s1, s2)    TIKU_LIST_CONCAT2(s1, s2)
/** @endcond */

/**
 * @def TIKU_LIST(name)
 * @brief Declare a statically-allocated linked list
 *
 * The list variable is declared static so it can be used within a
 * single compilation unit without polluting the global namespace.
 *
 * @param name The name of the list handle
 */
#define TIKU_LIST(name)                                                    \
    static void *TIKU_LIST_CONCAT(name, _list) = NULL;                    \
    static tiku_list_t name =                                              \
        (tiku_list_t)&TIKU_LIST_CONCAT(name, _list)

/**
 * @def TIKU_LIST_STRUCT(name)
 * @brief Declare a linked list as a member of a struct
 *
 * The list must be initialized with TIKU_LIST_STRUCT_INIT() before use.
 *
 * @param name The name of the list member
 */
#define TIKU_LIST_STRUCT(name)                                             \
    void *TIKU_LIST_CONCAT(name, _list);                                   \
    tiku_list_t name

/**
 * @def TIKU_LIST_STRUCT_INIT(struct_ptr, name)
 * @brief Initialize a list that is part of a struct
 *
 * @param struct_ptr Pointer to the containing struct
 * @param name       Name of the list member
 */
#define TIKU_LIST_STRUCT_INIT(struct_ptr, name)                            \
    do {                                                                   \
        (struct_ptr)->name =                                               \
            &((struct_ptr)->TIKU_LIST_CONCAT(name, _list));                \
        (struct_ptr)->TIKU_LIST_CONCAT(name, _list) = NULL;               \
        list_init((struct_ptr)->name);                                     \
    } while (0)

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a list to empty
 *
 * @param list The list to initialize
 */
void list_init(tiku_list_t list);

/**
 * @brief Get the first element of a list
 *
 * The element is not removed from the list.
 *
 * @param list The list
 * @return Pointer to the first element, or NULL if empty
 */
void *list_head(tiku_list_t list);

/**
 * @brief Get the last element of a list
 *
 * Traverses the entire list (O(n)). The element is not removed.
 *
 * @param list The list
 * @return Pointer to the last element, or NULL if empty
 */
void *list_tail(tiku_list_t list);

/**
 * @brief Remove and return the first element
 *
 * @param list The list
 * @return Pointer to the removed element, or NULL if empty
 */
void *list_pop(tiku_list_t list);

/**
 * @brief Add an item to the front of the list
 *
 * If the item is already in the list it is first removed to
 * prevent duplicate entries.
 *
 * @param list The list
 * @param item Item to add
 */
void list_push(tiku_list_t list, void *item);

/**
 * @brief Remove and return the last element
 *
 * @param list The list
 * @return Pointer to the removed element, or NULL if empty
 */
void *list_chop(tiku_list_t list);

/**
 * @brief Add an item to the end of the list
 *
 * If the item is already in the list it is first removed to
 * prevent duplicate entries.
 *
 * @param list The list
 * @param item Item to add
 */
void list_add(tiku_list_t list, void *item);

/**
 * @brief Remove a specific item from the list
 *
 * Does nothing if the item is NULL or not found in the list.
 *
 * @param list The list
 * @param item Item to remove
 */
void list_remove(tiku_list_t list, void *item);

/**
 * @brief Count the number of elements in the list
 *
 * @param list The list
 * @return Number of elements
 */
int list_length(tiku_list_t list);

/**
 * @brief Shallow-copy a list handle
 *
 * Copies the head pointer only; the elements are shared.
 *
 * @param dest Destination list
 * @param src  Source list
 */
void list_copy(tiku_list_t dest, tiku_list_t src);

/**
 * @brief Insert an item after a specified item
 *
 * If previtem is NULL the new item is pushed to the front.
 * If the item is already in the list it is first removed to
 * prevent duplicate entries.
 *
 * @param list     The list
 * @param previtem Item after which to insert (or NULL for front)
 * @param newitem  Item to insert
 */
void list_insert(tiku_list_t list, void *previtem, void *newitem);

/**
 * @brief Get the next item after the given item
 *
 * @param item A list item
 * @return Next item, or NULL if at end of list
 */
void *list_item_next(void *item);

#endif /* TIKU_LIST_H_ */
