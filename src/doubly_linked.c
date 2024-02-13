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

#include "doubly_linked.h"
#include <stdlib.h>

void doubly_linked_create(doubly_linked_t *list, size_t max_length, item_deallocator_t deallocator)
{
    list->head = list->tail = NULL;
    list->max_length = max_length;
    list->length = 0;

    // Storing a deallocator function so as to make the API simpler and less verbose
    list->deallocator = deallocator;
}

void doubly_linked_insert_first(doubly_linked_t *list, void *data)
{
    doubly_node_t *new_node = malloc(sizeof(doubly_node_t));
    new_node->data = data;
    
    // If both the head and tail are empty, make them both point to the new item
    if (list->length == 0)
    {
        list->head = list->tail = new_node;
    }
    else
    {
        // Make the next item the new root
        new_node->previous = list->head;
        list->head->next = new_node;
        list->head = new_node;
    }

    // If we've reached the size limit, clear the last entry
    if (list->length == list->max_length - 1)
    {
        doubly_node_t *node_to_delete = list->tail;
        list->tail = list->tail->previous;

        list->deallocator(node_to_delete->data);
        free(node_to_delete);

        list->length--;
    }
    
    list->length++;
}

void doubly_linked_delete_head(doubly_linked_t *list)
{
    // If there are no previous entries, ignore the request
    if (list->length < 2) return;

    doubly_node_t *node_to_delete = list->head;
    list->head = list->head->previous;

    list->deallocator(node_to_delete->data);
    free(node_to_delete);

    list->length--;
}
