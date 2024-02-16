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

#ifndef _GEMINI_H
#define _GEMINI_H

#include <stddef.h>
#include <openssl/ssl.h>
#include "dynamic_array.h"

typedef enum
{
    GEMTEXT_PARAGRAPH,
    GEMTEXT_PREFORMATTED,
    GEMTEXT_LINK,
    GEMTEXT_HEADING_ONE,
    GEMTEXT_HEADING_TWO,
    GEMTEXT_HEADING_THREE,
    GEMTEXT_BLOCKQUOTE,
    GEMTEXT_LIST_ITEM
} gemtext_line_e;

typedef enum
{
    GEMINI_OK,
    GEMINI_TEMPORARY_FAILURE,
    GEMINI_PERMANENT_FAILURE,
    GEMINI_CLIENT_CERTIFICATE_REQUIRED,
    GEMINI_IP_RESOLVE_FAILURE,
    GEMINI_SERVER_CONNECTION_FAILURE,
    
    GEMINI_TLS_HANDSHAKE_FAILURE,
    GEMINI_NOT_TEXT,
    GEMINI_HEADER_PARSING_FAILURE,
    // Is this even a word?
    TOTAL_GEMINI_ERRORS
} gemini_error_e;

typedef struct
{
    gemtext_line_e type;

    // The boundaries with respect to the content string
    size_t start, end;
} gemtext_line_t;

typedef struct
{
    DYN_ARRAY(char) content;
    DYN_ARRAY(gemtext_line_t) elements;

    char *url;
    gemini_error_e error;
} gemini_document_t;

typedef size_t (*gemini_input_callback_t) (char *buffer, char *prompt, size_t max_length);

// Initializes and populates a gemini document by accessing the provided server using the Gemini protocol
gemini_document_t* gemini_fetch_document(SSL_CTX *ctx, char *gemini_url, gemini_input_callback_t input_callback);
void gemini_document_parse_gemtext(gemini_document_t *document);
void gemini_document_destroy(gemini_document_t *document);

#endif
