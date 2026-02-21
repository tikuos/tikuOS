/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * list.c - Generic singly-linked list library implementation
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

#include "list.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE TYPES                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Internal node representation
 *
 * Every item in the list must have a pointer as its first member.
 * This struct lets the library follow the chain without knowing
 * the concrete element type.
 */
struct list {
    struct list *next;
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize a list to empty
 *
 * @param list The list to initialize
 */
void list_init(tiku_list_t list)
{
    *list = NULL;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Get the first element of a list
 *
 * @param list The list
 * @return Pointer to the first element, or NULL if empty
 */
void *list_head(tiku_list_t list)
{
    return *list;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Shallow-copy a list handle
 *
 * @param dest Destination list
 * @param src  Source list
 */
void list_copy(tiku_list_t dest, tiku_list_t src)
{
    *dest = *src;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Get the last element of a list
 *
 * @param list The list
 * @return Pointer to the last element, or NULL if empty
 */
void *list_tail(tiku_list_t list)
{
    struct list *l;

    if (*list == NULL) {
        return NULL;
    }

    for (l = *list; l->next != NULL; l = l->next) {
        /* traverse */
    }

    return l;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Add an item to the end of the list
 *
 * Removes the item first if it is already present, preventing
 * duplicate entries.
 *
 * @param list The list
 * @param item Item to add
 */
void list_add(tiku_list_t list, void *item)
{
    struct list *l;

    if (item == NULL) {
        return;
    }

    /* Prevent duplicate entries */
    list_remove(list, item);

    ((struct list *)item)->next = NULL;

    l = list_tail(list);

    if (l == NULL) {
        *list = item;
    } else {
        l->next = item;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Add an item to the front of the list
 *
 * Removes the item first if it is already present, preventing
 * duplicate entries.
 *
 * @param list The list
 * @param item Item to add
 */
void list_push(tiku_list_t list, void *item)
{
    if (item == NULL) {
        return;
    }

    /* Prevent duplicate entries */
    list_remove(list, item);

    ((struct list *)item)->next = *list;
    *list = item;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Remove and return the last element
 *
 * @param list The list
 * @return Pointer to the removed element, or NULL if empty
 */
void *list_chop(tiku_list_t list)
{
    struct list *l, *r;

    if (*list == NULL) {
        return NULL;
    }

    if (((struct list *)*list)->next == NULL) {
        l = *list;
        *list = NULL;
        return l;
    }

    for (l = *list; l->next->next != NULL; l = l->next) {
        /* traverse to second-to-last */
    }

    r = l->next;
    l->next = NULL;

    return r;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Remove and return the first element
 *
 * @param list The list
 * @return Pointer to the removed element, or NULL if empty
 */
void *list_pop(tiku_list_t list)
{
    struct list *l;

    l = *list;
    if (l != NULL) {
        *list = l->next;
        l->next = NULL;
    }

    return l;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Remove a specific item from the list
 *
 * Does nothing if item is NULL or not found.
 *
 * @param list The list
 * @param item Item to remove
 */
void list_remove(tiku_list_t list, void *item)
{
    struct list *l, *prev;

    if (*list == NULL || item == NULL) {
        return;
    }

    prev = NULL;
    for (l = *list; l != NULL; l = l->next) {
        if (l == item) {
            if (prev == NULL) {
                *list = l->next;
            } else {
                prev->next = l->next;
            }
            l->next = NULL;
            return;
        }
        prev = l;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Count the number of elements in the list
 *
 * @param list The list
 * @return Number of elements
 */
int list_length(tiku_list_t list)
{
    struct list *l;
    int n = 0;

    for (l = *list; l != NULL; l = l->next) {
        ++n;
    }

    return n;
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Insert an item after a specified item
 *
 * If previtem is NULL the new item is pushed to the front.
 * Removes the item first if it is already present, preventing
 * duplicate entries.
 *
 * @param list     The list
 * @param previtem Item after which to insert (or NULL for front)
 * @param newitem  Item to insert
 */
void list_insert(tiku_list_t list, void *previtem, void *newitem)
{
    if (newitem == NULL) {
        return;
    }

    /* Prevent duplicate entries */
    list_remove(list, newitem);

    if (previtem == NULL) {
        list_push(list, newitem);
    } else {
        ((struct list *)newitem)->next =
            ((struct list *)previtem)->next;
        ((struct list *)previtem)->next = newitem;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Get the next item after the given item
 *
 * @param item A list item
 * @return Next item, or NULL if item is NULL or at end of list
 */
void *list_item_next(void *item)
{
    if (item == NULL) {
        return NULL;
    }
    return ((struct list *)item)->next;
}
