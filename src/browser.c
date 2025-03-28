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
#include <unistd.h>

// Some global, statically allocated strings for the implementation of some simple error handling
static char *error_format = "# Astrology: Gemini Request Failed\n> Error Details: %s"
    " If the error persists and does not occur on any other Gemini client, please report it!"
    " As of now, you can just revert to the previous history entry and continue browsing.";

static char *gemini_error_mappings[TOTAL_GEMINI_ERRORS] = {
    [GEMINI_IP_RESOLVE_FAILURE] = "Failed to resolve the IP address of the server.",
    [GEMINI_SERVER_CONNECTION_FAILURE] = "Failed to establish a simple TCP connection with the server.",
    [GEMINI_TEMPORARY_FAILURE] = "The server encountered a temporary failure.",
    [GEMINI_PERMANENT_FAILURE] = "The server encountered a permanent failure.",
    [GEMINI_CLIENT_CERTIFICATE_REQUIRED] = "The server requires a client certificate, which is not implemented yet.",
    [GEMINI_TLS_HANDSHAKE_FAILURE] = "The TLS handshake failed, is the server down?",
    [GEMINI_NOT_TEXT] = "The server returned something that is neither gemtext nor raw text, cannot render!",
    [GEMINI_HEADER_PARSING_FAILURE] = "Failed to parse the server's response header. Is the server properly implemented?",
};

// Will be passed as an item deallocator into the generic doubly linked list instance
static void page_deallocator(void *data)
{
    gemini_page_t *page = (gemini_page_t*) data;
    gemini_document_destroy(page->document);

    free(page);
}

void gemini_browser_create(gemini_browser_t *browser, gemini_input_callback_t input_callback)
{
    // Load in the booksmarks into memory
    // Ensure that the file actually exists
    // If it does not, it will be created right before the program terminates
    if (access("bookmarks", F_OK) >= 0)
    {
        FILE *bookmarks_file = fopen("bookmarks", "r");
        if (!bookmarks_file)
            exit_with_failure("failed to open booksmarks file, although it does exist");

        char *line = NULL;
        size_t len = 0;
        size_t bytes_read;
        for (int i = 0; i != 9 && (bytes_read = getline(&line, &len, bookmarks_file)) != -1; i++)
        {
            // If the line is empty, just ignore it
            if (bytes_read < 3) continue;

            // Ignore the new line character located at the end
            if (line[bytes_read - 1] == '\n')
                bytes_read--;
        
            strncpy(browser->bookmarks[i], line, bytes_read);
        }

        free(line);
        fclose(bookmarks_file);
    }
    
    // The SSL context will describe how future SSL connection will be created
    // The latest TLS method will be used
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    browser->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!browser->ssl_ctx)
        exit_with_failure("failed to initialize TLS client context");

#ifdef WITH_SSL_CERT
        // If WITH_SSL_CERT is defined then load the CA certificates for verification
    
    if (!SSL_CTX_load_verify_locations(browser->ssl_ctx, CERTIFICATION_PATH, NULL))
        exit_with_failure("failed to load CA certificates");
    
    // Set the verification mode to for a valid certificate
    SSL_CTX_set_verify(browser->ssl_ctx, SSL_VERIFY_PEER, NULL);
    
#endif

    // Initializing the pages doubly linked list that will act as a history recorder
    doubly_linked_create(&browser->pages, MAX_HISTORY_LENGTH, page_deallocator);

    browser->input_callback = input_callback;
}

void gemini_browser_load_document(gemini_browser_t *browser, char *gemini_url)
{
    gemini_page_t *page = malloc(sizeof(gemini_page_t));

    page->scroll_offset = 0;
    page->document = gemini_fetch_document(browser->ssl_ctx, gemini_url, browser->input_callback);

    /*
     * Check if any errors were encountered
     * The program will notify the user by inserting the notice into the document
     * The frontend is not required to take any further action
     */
    if (page->document->error != GEMINI_OK)
    {
        char *error_message = gemini_error_mappings[page->document->error];
        
        size_t error_buffer_length = strlen(error_format) - strlen("%s") + strlen(error_message);
        page->document->content = dyn_array_create(error_buffer_length + 1, sizeof(char));

        sprintf(page->document->content, error_format, error_message);
        page->document->content[error_buffer_length] = 0;
        *DYN_ARRAY_GET_ATTRIBUTE(page->document->content, DYN_ARRAY_LENGTH) = error_buffer_length;
        
        gemini_document_parse_gemtext(page->document);
    }

    doubly_linked_insert_first(&browser->pages, page);
}

void gemini_browser_go_back(gemini_browser_t *browser)
{
    doubly_linked_delete_head(&browser->pages);
}

void browser_destroy(gemini_browser_t *browser)
{
    // Just save the modified bookmarks into the file again
    FILE *bookmarks_file = fopen("bookmarks", "w");
    if (!bookmarks_file)
        exit_with_failure("failed to open bookmarks file while trying to save");

    for (int i = 0; i < 9; i++)
    {
        if (browser->bookmarks[i][0])
        {
            fwrite(browser->bookmarks[i], sizeof(char), strlen(browser->bookmarks[i]), bookmarks_file);
        }

        // Separate lines with a newline
        fputc('\n', bookmarks_file);
    }

    fclose(bookmarks_file);
    doubly_linked_destroy(&browser->pages);
    SSL_CTX_free(browser->ssl_ctx);
}

void gemini_browser_get_link_under_cursor(gemini_browser_t *browser, browser_link_t *link)
{
    // A static enum list to associate link types with schemes
    static char *scheme_mappings[TOTAL_SCHEMES] = {
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

    // First, extract the actual link content into a separate string
    int start, end;
    for (start = element->start + 2; isspace(content[start]); start++);
    for (end = start; end != element->end && !isspace(content[end + 1]); end++);

    char *link_string = malloc(end - start + 2);
    strncpy(link_string, page->document->content + start, end - start + 1);
    link_string[end - start + 1] = 0;

    if (has_protocol_scheme(link_string))
    {
        link->content = link_string;
    }
    else
    {
        link->content = join_relative_link_to_url(page->document->url, link_string);
        free(link_string);
    }

    // Find out the type of the link's scheme
    for (int i = 0; i < TOTAL_SCHEMES; i++)
    {
        if (!strncmp(link->content, scheme_mappings[i], strlen(scheme_mappings[i])))
        {
            link->scheme = i;
            return;
        }
    }
}

void browser_link_destroy(browser_link_t *link)
{
    free(link->content);
}
