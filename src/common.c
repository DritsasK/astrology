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

#include "common.h"
#include <stdio.h>
#include <ncurses.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// The length is supplied to so that the user can provide only a portion of the string
char* join_strings_together(char *first, size_t first_len, char *second, size_t second_len)
{
    char *result = malloc(first_len + second_len + 1);

    strncpy(result, first, first_len);
    strncpy(result + first_len, second, second_len);
    result[first_len + second_len] = 0;

    return result;
}

char* get_hostname_with_scheme(char *url)
{
    char *colon = strchr(url, ':');
    // Skip the first two scheme slashes and find the host
    char *slash = strchr(colon + 3, '/');
    
    if (slash)
    {
        size_t slash_offset = slash - url;
        char *hostname = malloc((slash_offset + 1) * sizeof(char));
        strncpy(hostname, url, slash_offset);
        hostname[slash_offset] = 0;

        return hostname;
    }

    // No slash was found, return as is
    return strdup(url);
}

void exit_with_failure(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    endwin();

    // Just print out the error message with a fancy format
    fprintf(stderr, "{astrology error}: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    // Terminate the execution of the program
    exit(EXIT_FAILURE);
}
