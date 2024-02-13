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

#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>

// A collection of some handy macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))

// Allocates memory and concatenates the string together
// The return value must be freed
char* join_strings_together(char *first, size_t first_len, char *second, size_t second_len);

// The url must include a scheme
// Make sure to free the returned value
char* get_hostname_with_scheme(char *url);

void exit_with_failure(const char *format, ...);

#endif
