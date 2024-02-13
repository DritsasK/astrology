/* Astrology
 * Copyright (C) 2024 Petros Katiforis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _DOUBLY_LINKED_H
#define _DOUBLY_LINKED_H

#include <stddef.h>

typedef struct doubly_node_t
{
    struct doubly_node_t *next;
    struct doubly_node_t *previous;

    void *data;
} doubly_node_t;

typedef void (*item_deallocator_t) (void *item);

typedef struct
{
    doubly_node_t *head;
    // Keeping track of the last item to make deletion faster
    doubly_node_t *tail;

    item_deallocator_t deallocator;
    size_t max_length;
    size_t length;
} doubly_linked_t;

void doubly_linked_create(doubly_linked_t *list, size_t max_length, item_deallocator_t deallocator);
void doubly_linked_insert_first(doubly_linked_t *list, void *data);
void doubly_linked_delete_head(doubly_linked_t *list);

#endif
