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

int get_hostname_length(char *url)
{
    char *colon = strchr(url, ':');
    // Skip the first two scheme slashes and find the host
    char *slash = strchr(colon + 3, '/');

    return slash ? slash - url : strlen(url);
}

char* get_hostname_with_scheme(char *url)
{
    int hostname_length = get_hostname_length(url);
    
    if (hostname_length != strlen(url))
    {
        char *hostname = malloc((hostname_length + 1) * sizeof(char));
        strncpy(hostname, url, hostname_length);
        hostname[hostname_length] = 0;

        return hostname;
    }

    // No extra slash was found, return as is
    return strdup(url);

}

bool has_protocol_scheme(char *url)
{
    bool has_protocol = false;

    // No need to check every single character, just pick the first eight
    for (int i = 0; url[i] && i < 8; i++)
    {
        if (url[i] == ':')
        {
            has_protocol = true;
            break;
        }
    }

    return has_protocol;
}

char* join_relative_link_to_url(char *current_url, char *link)
{
    // Check if the link is relative to the hostname
    if (link[0] == '/')
    {
        int host_length = get_hostname_length(current_url);
        return join_strings_together(
            current_url, host_length,
            link, strlen(link));
    }

    // Otherwise, it must be relative to the current directory
    // A link of `note.gmi` - without a starting slash - is an example of such case
    else
    {
        size_t base_url_len = strlen(current_url);
            
        for (int i = base_url_len; i > 0; i--)
        {
            if (current_url[i] == '/')
            {
                return join_strings_together(
                    current_url, i + 1,
                    link, strlen(link));
            }
        }
    }
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
