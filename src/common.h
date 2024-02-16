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
#include <stdbool.h>

// A collection of some handy macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

// Allocates memory and concatenates the strings together
// The return value must be freed
char* join_strings_together(char *first, size_t first_len, char *second, size_t second_len);

// The url must strictly include a scheme
// The final slash character (if it exists) is not considered part of it
int get_hostname_length(char *url);

// The final slash (if it does exist) will not be included
char* get_hostname_with_scheme(char *url);

bool has_protocol_scheme(char *url);
char* join_relative_link_to_url(char *current_url, char *link);

void exit_with_failure(const char *format, ...);

#endif
