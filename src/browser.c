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

#include "browser.h"
#include "common.h"
#include <ctype.h>

// Some globals, statically allocated strings for the implementation of some simple error handling
static char* error_format = "# Astrology: Gemini Request Failed\n> Error Details: %s"
    " If the error persists and does not occur on any other Gemini client, please report it!"
    " As of now, you can just revert to the previous history entry and continue browsing.";

static char* gemini_error_mappings[TOTAL_GEMINI_STATUSES] = {
    [GEMINI_IP_RESOLVE_FAILURE] = "Failed to resolve the IP address of the server.",
    [GEMINI_SERVER_CONNECTION_FAILURE] = "Failed to establish a simple TCP connection with the server.",
    [GEMINI_TEMPORARY_FAILURE] = "The server encountered a temporary failure.",
    [GEMINI_PERMANENT_FAILURE] = "The server encountered a permanent failure.",
    [GEMINI_CLIENT_CERTIFICATE_REQUIRED] = "The server requires a client certificate, which is not implemented yet.",
    [GEMINI_TLS_HANDSHAKE_FAILURE] = "The TLS handshake failed, is the server down?",
    [GEMINI_NOT_GEMTEXT] = "The server returned something that is not Gemtext, cannot render!",
    [GEMINI_HEADER_PARSING_FAILURE] = "Failed to parse the server's response header. Is the server properly implemented?",
};

// Will be passed as an item deallocator into the generic doubly linked list instance
static void page_deallocator(void *data)
{
    gemini_page_t *page = (gemini_page_t*) data;

    gemini_document_destroy(page->document);
    free(page);
}

void gemini_browser_create(gemini_browser_t *browser)
{
    // The SSL context will describe how future SSL connection will be created
    // The latest TLS method will be used
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    browser->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!browser->ssl_ctx)
        exit_with_failure("failed to initialize TLS client context");

    // Initializing the pages doubly linked list that will act as a history recorder
    doubly_linked_create(&browser->pages, MAX_HISTORY_LENGTH, page_deallocator);
}

void gemini_browser_load_document(gemini_browser_t *browser, char *gemini_url)
{
    gemini_page_t *page = malloc(sizeof(gemini_page_t));

    page->scroll_offset = 0;
    page->document = gemini_fetch_document(browser->ssl_ctx, gemini_url);

    /*
     * Check if any errors where encountered
     * The program will notify the user by inserting the notice into the document
     * The frontend is not required to take any further action
     */
    if (page->document->status != GEMINI_OK)
    {
        char *error_message = gemini_error_mappings[page->document->status];
        
        size_t error_buffer_length = strlen(error_format) - 2 + strlen(error_message);
        page->document->content = dyn_array_create(error_buffer_length + 1, sizeof(char));

        sprintf(page->document->content, error_format, error_message);
        page->document->content[error_buffer_length] = 0;
        *DYN_ARRAY_GET_ATTRIBUTE(page->document->content, DYN_ARRAY_LENGTH) = error_buffer_length;
        
        gemini_document_parse_content(page->document);
    }

    doubly_linked_insert_first(&browser->pages, page);
}

void gemini_browser_go_back(gemini_browser_t *browser)
{
    doubly_linked_delete_head(&browser->pages);
}

void gemini_browser_get_link_under_cursor(gemini_browser_t *browser, browser_link_t *link)
{
    // A static enum list to associate link types with schemes
    static char* scheme_mappings[TOTAL_SCHEMES] = {
        [LINK_SCHEME_GEMINI] = "gemini://",
        [LINK_SCHEME_HTTP] = "http://",
        [LINK_SCHEME_HTTPS] = "https://"
    };

    gemini_page_t *page = browser->pages.head->data;
    char *content = page->document->content;
    gemtext_line_t *element = &page->document->elements[page->scroll_offset];
    link->scheme = LINK_SCHEME_INVALID;
    
    // If the elements is not a link, ingore the request
    if (element->type != GEMTEXT_LINK)
        return;

    // First, get the boundaries of the URL
    int start, end;
    for (start = element->start + 2; isspace(content[start]); start++);
    for (end = start; end != element->end && !isspace(content[end + 1]); end++);
    link->length = end - start + 1;

    // Handle the easy case first:
    // If the link is relative to the hostname, append it to the active URL
    if (content[start] == '/')
    {
        char *hostname = get_hostname_with_scheme(page->document->url);
        link->scheme = LINK_SCHEME_GEMINI;
        link->content = join_strings_together(
            hostname, strlen(hostname),
            &content[start], link->length);
        
        free(hostname);
        return;
    }

    // Otherwise, check if the URL contains a protocol scheme
    // No need to check every single character, just pick the first eight
    bool has_protocol = false;
    
    for (int i = start; i < MIN(end, start + 8); i++)
    {
        if (content[i] == ':')
        {
            has_protocol = true;
            break;
        }
    }

    if (has_protocol)
    {
        // Find the type of the link
        for (int i = 0; i < TOTAL_SCHEMES; i++)
        {
            if (!strncmp(content + start, scheme_mappings[i], strlen(scheme_mappings[i])))
            {
                char *new_url = malloc(link->length + 1);
                strncpy(new_url, content + start, link->length);
                new_url[link->length] = 0;

                link->content = new_url;
                link->scheme = i;
                return;
            }
        }
    }
    // If it does not contain a protocol but it isn't relative either,
    // it must be pointing to another location at the current directory
    else
    {
        char *browser_url = page->document->url;
        size_t browser_url_len = strlen(browser_url);
            
        for (int i = browser_url_len; i > 0; i--)
        {
            if (browser_url[i] == '/')
            {
                link->scheme = LINK_SCHEME_GEMINI;

                // Just append to that base the path the cursor
                link->content = join_strings_together(
                    browser_url, i +  1,
                    content + start, end - start + 1);

                return;
            }
        }
    }
}

void browser_link_destroy(browser_link_t *link)
{
    free(link->content);
}
