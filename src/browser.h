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

#ifndef _GEMINI_BROWSER_H
#define _GEMINI_BROWSER_H

#include "gemini.h"
#include "config.h"
#include "doubly_linked.h"
#include <stddef.h>
#include <stdbool.h>
#include <openssl/ssl.h>

typedef struct
{
    gemini_document_t *document;
    int scroll_offset;
} gemini_page_t;

typedef enum
{
    LINK_SCHEME_GEMINI,
    LINK_SCHEME_HTTP,
    LINK_SCHEME_HTTPS,
    TOTAL_SCHEMES,
    
    LINK_SCHEME_INVALID
} link_scheme_e;

typedef struct
{
    char *content;
    size_t length;
    link_scheme_e scheme;
} browser_link_t;

typedef struct
{
    // The same context will be used throughout all gemini connections
    SSL_CTX *ssl_ctx;

    doubly_linked_t pages;
    gemini_input_callback_t input_callback;
} gemini_browser_t;

// This function must be called before any document has been loaded
void gemini_browser_create(gemini_browser_t *browser, gemini_input_callback_t input_callback);

void gemini_browser_load_document(gemini_browser_t *browser, char *gemini_url);
void gemini_browser_go_back(gemini_browser_t *browser);

void gemini_browser_get_link_under_cursor(gemini_browser_t *browser, browser_link_t *link);
void browser_link_destroy(browser_link_t *link);

#endif
