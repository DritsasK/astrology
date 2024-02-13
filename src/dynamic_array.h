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

#ifndef _DYNAMIC_ARRAY_H
#define _DYNAMIC_ARRAY_H

#include <stddef.h>

typedef enum
{
    DYN_ARRAY_LENGTH,
    DYN_ARRAY_CAPACITY,
    DYN_ARRAY_ITEM_SIZE,
    DYN_ARRAY_HEADER_SIZE
} dyn_array_attribute_e;

typedef void* dyn_array_t;

// Will return a pointer for both read and write operations
#define DYN_ARRAY_GET_ATTRIBUTE(array, attr) ((size_t*) array - DYN_ARRAY_HEADER_SIZE + attr)
#define DYN_ARRAY_LENGTH(array) *DYN_ARRAY_GET_ATTRIBUTE(array, DYN_ARRAY_LENGTH)
#define DYN_ARRAY_GET_LAST(array) array[DYN_ARRAY_LENGTH(array) - 1];

// Just so there exists some form of differentiation between ordinary and dynamic arrays
#define DYN_ARRAY(type) type*

dyn_array_t dyn_array_create(size_t initial_capacity, size_t item_size);

// Will copy the contents of the buffer directly into the array, incrementing the length accordingly
// The dynamic array needs to be reassigned in case that it has been silently resized
dyn_array_t dyn_array_append_buffer(dyn_array_t array, void *buffer, int total_items);

// Prepares the addition of a new item into the array
// Will resize if needed, so a reassignment is once again needed
dyn_array_t dyn_array_prepare_new_item(dyn_array_t array);

void dyn_array_destroy(dyn_array_t array);

#endif
