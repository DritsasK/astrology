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

#include "dynamic_array.h"
#include <stdlib.h>
#include <string.h>
#include "common.h"

dyn_array_t dyn_array_create(size_t initial_capacity, size_t item_size)
{
    // Allocate enough space for both the header and the actual data
    size_t *array = malloc(DYN_ARRAY_HEADER_SIZE * sizeof(size_t) + item_size * initial_capacity);

    array[DYN_ARRAY_LENGTH] = 0;
    array[DYN_ARRAY_CAPACITY] = initial_capacity;
    array[DYN_ARRAY_ITEM_SIZE] = item_size;

    // Hide the header and return the data pointer
    // The API will be almost identical to that of an ordinary array
    return array + DYN_ARRAY_HEADER_SIZE;
}

dyn_array_t dyn_array_resize_to_fit(dyn_array_t array, size_t total_items)
{
    size_t *capacity = DYN_ARRAY_GET_ATTRIBUTE(array, DYN_ARRAY_CAPACITY);
    
    if (total_items > *capacity)
    {
        *capacity = MAX(total_items, *capacity * 2);
        
        size_t *actual_array = (size_t*) array - DYN_ARRAY_HEADER_SIZE;
        actual_array = realloc(actual_array, DYN_ARRAY_HEADER_SIZE * sizeof(size_t) +
                               *capacity * *DYN_ARRAY_GET_ATTRIBUTE(array, DYN_ARRAY_ITEM_SIZE));
        
        // Point back to the start of the actual data
        return actual_array + DYN_ARRAY_HEADER_SIZE;
    }

    return array;
}

dyn_array_t dyn_array_prepare_new_item(dyn_array_t array)
{
    *DYN_ARRAY_GET_ATTRIBUTE(array, DYN_ARRAY_LENGTH) += 1;
    return dyn_array_resize_to_fit(array, DYN_ARRAY_LENGTH(array));
}

void dyn_array_destroy(dyn_array_t array)
{
    // Just clear up the initially allocated memory
    // Make sure to call free at the actual start, not the API-friendly position
    free(array - DYN_ARRAY_HEADER_SIZE * sizeof(size_t));
}
