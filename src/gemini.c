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

#include "gemini.h"
#include "common.h"
#include "dynamic_array.h"
#include <sys/socket.h>
#include <stdbool.h>
#include <ctype.h>
#include <netdb.h>
#include <unistd.h>

// Returns the file descriptor of the connection's socket
static int create_ordinary_tcp_connection(const char *hostname, gemini_error_e *status)
{
    struct addrinfo dns_hints = {
        // Use IPv4 or IPv6, whatever is available
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    
    // Collecting the IP address of the server
    // A linked list will be returned resulting from DNS lookup process
    struct addrinfo *server_info;
    
    if (getaddrinfo(hostname, "1965", &dns_hints, &server_info) != 0)
    {
        *status = GEMINI_IP_RESOLVE_FAILURE;
        return -1;
    }

    // Create a TCP connection
    int connection = socket(server_info->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (socket < 0)
        exit_with_failure("failed to initialize TCP socket");

    // Attempt to connect and clear up all memory
    if (connect(connection, (struct sockaddr*) server_info->ai_addr, server_info->ai_addrlen) != 0)
    {
        *status = GEMINI_SERVER_CONNECTION_FAILURE;
        return -1;
    }

    *status = GEMINI_OK;
    freeaddrinfo(server_info);
    return connection;
}

static void gemini_document_collect_content(gemini_document_t *document, SSL *ssl)
{
    size_t chunk_size = 1024;
    
    // First, just read of all the content into a dynamic array
    // It will make parsing considerably easier
    document->content = dyn_array_create(chunk_size, sizeof(char));
    
    for (;;)
    {
        size_t length = DYN_ARRAY_LENGTH(document->content);
        size_t remaining_space = *DYN_ARRAY_GET_ATTRIBUTE(document->content, DYN_ARRAY_CAPACITY) - length;

        if (remaining_space < chunk_size)
        {
            // This is really similar to how Golang works
            document->content = dyn_array_resize_to_fit(document->content, length + chunk_size);
        }

        // This is more complicated but certainly faster that creating an intermediate buffer
        int bytes_read = SSL_read(ssl, document->content + length, chunk_size);
        if (bytes_read <= 0)
            break;
 
        *DYN_ARRAY_GET_ATTRIBUTE(document->content, DYN_ARRAY_LENGTH) += bytes_read;
    }

    document->content[DYN_ARRAY_LENGTH(document->content)] = 0;
}

/*
 * Raw text content will be parsed into a fake list of preformatted gemtext elements
 * The frontend will be simplified too, because it won't need to distinguish them apart!
 */
static void gemini_document_parse_text(gemini_document_t *document)
{
    // Count the total line breaks, much faster than resizing the dynamic array
    size_t total_lines = 0;

    for (int i = 0; i < DYN_ARRAY_LENGTH(document->content); i++)
        if (document->content[i] == '\n')
            total_lines++;

    document->elements = dyn_array_create(total_lines, sizeof(gemtext_line_t));
    size_t offset = 0;
    
    for (size_t i = 0; i < total_lines; i++)
    {
        document->elements = dyn_array_prepare_new_item(document->elements);
        gemtext_line_t *new_item = &DYN_ARRAY_GET_LAST(document->elements);

        new_item->start = offset;
        new_item->type = GEMTEXT_PREFORMATTED;

        if (i != total_lines - 1)
        {
            // Find the next new line position and perform a simple subtraction to calculate the substring's end
            char *next_new_line = strchr(document->content + offset, '\n');

            new_item->end = (next_new_line - document->content);
            offset = new_item->end + 1;
        }
        else
            new_item->end = DYN_ARRAY_LENGTH(document->content);
    }
}

/*
 * Assigns a type to the current line based on its prefix characters
 * Will gracefully handle the cases in which some tokens appear right at the end of the buffer
 */
static gemtext_line_e get_gemtext_type_from_line(char *line)
{
    switch (line[0])
    {
    case '>': return GEMTEXT_BLOCKQUOTE;
    case '*': return GEMTEXT_LIST_ITEM;

    case '#':
        // Collect the level of the heading and avoid an out-of-bounds runtime error
        if (line[1] && line[2] == '#') return GEMTEXT_HEADING_THREE;
        if (line[1] == '#') return GEMTEXT_HEADING_TWO;

        return GEMTEXT_HEADING_ONE;
    }

    if (!strncmp("=>", line, 2))  return GEMTEXT_LINK;
    if (!strncmp("```", line, 3)) return GEMTEXT_PREFORMATTED;

    // If nothing special was recognized, it must be a plain paragraph
    return GEMTEXT_PARAGRAPH;
}

void gemini_document_parse_gemtext(gemini_document_t *document)
{
    // Parsing each line of the output into an array of gemtext elements
    document->elements = dyn_array_create(20, sizeof(gemtext_line_t));
    
    size_t offset = 0;
    size_t content_length = DYN_ARRAY_LENGTH(document->content);
    bool has_entered_preformatted = false;
    
    while (offset < content_length)
    {
        // Skip all trailing new line characters if we are not inside a preformatted block
        if (!has_entered_preformatted)
        {
            while (isspace(document->content[offset])) offset++;
            if (offset > content_length - 1) break;
        }

        document->elements = dyn_array_prepare_new_item(document->elements);
        gemtext_line_t *new_item = &DYN_ARRAY_GET_LAST(document->elements);

        new_item->start = offset;
        new_item->type = get_gemtext_type_from_line(document->content + offset);

        if (new_item->type == GEMTEXT_PREFORMATTED)
            has_entered_preformatted = !has_entered_preformatted;

        // If we're still inside a preformatted block, overwrite whatever type was detected
        else if (has_entered_preformatted)
            new_item->type = GEMTEXT_PREFORMATTED;
        
        // Advance until either a new line or the end of the buffer has been reached
        new_item->end = offset;
        while (new_item->end != content_length - 1 && document->content[new_item->end] != '\n') new_item->end++;

        // If we've reached the end of the buffer, finish
        if (new_item->end == content_length - 1)
            break;

        new_item->end--;
        offset = new_item->end + 2;
    }
}

gemini_document_t* gemini_fetch_document(SSL_CTX *ctx, char *gemini_url, gemini_input_callback_t input_callback)
{
    // Create a new TLS connection using the provided context
    SSL *ssl = SSL_new(ctx);
    gemini_error_e error;

    char *hostname = get_hostname_with_scheme(gemini_url);
    int connection = create_ordinary_tcp_connection(hostname + 9, &error);
    free(hostname);

    // Quit early if an error was encountered during the simple socket connection
    if (error != GEMINI_OK)
        goto fetch_failed;

    SSL_set_fd(ssl, connection);

    if (SSL_connect(ssl) != 1)
    {
        error = GEMINI_TLS_HANDSHAKE_FAILURE;
        goto fetch_failed;
    }

    // NOTE: This is where I should have been validating the server's certificate
    // I might implement TOFU certificates in the future
    
    // 1029 is the maximum size of the server response header (plus a NULL byte)
    // This buffer will be used for both auxiliary input and output storage
    char io_buffer[1030];
    
    // The client needs to request a gemini page from the server.
    // A scheme should be included and the request shall be terminated with a carriage return followed by a newline
    sprintf(io_buffer, "%s\r\n", gemini_url);
    size_t url_len = strlen(gemini_url);
    SSL_write(ssl, io_buffer, url_len + 2);
    
    // Read and parse the server's response header
    // Gemini server headers are of the form: <2 bytes: STATUS><SPACE><1024 bytes: META>\r\n
    size_t header_length = 0;
    for (;;)
    {
        int bytes_read = SSL_read(ssl, io_buffer + header_length, 1);
        if (!bytes_read)
            break;
        
        header_length++;

        // Check if we've reached the end of the buffer
        // I'm doing this one character at a time so I don't skip some content by accident
        if (header_length >= 2 && io_buffer[header_length - 2] == '\r' && io_buffer[header_length - 1] == '\n')
        {
            io_buffer[header_length] = 0;
            break;
        }
    }
    
    // If not even a status was received, something went wrong
    if (header_length < 4)
    {
        error = GEMINI_HEADER_PARSING_FAILURE;
        goto fetch_failed;
    }

    char status[2];
    char meta[1024];
    sscanf(io_buffer, "%2s %s\r\n", status, meta);
    
    switch (status[0])
    {
    case '1':
        // The result query (e.g. ?search%20query) will be glued to the initial URL and the request will be repeated
        strncpy(io_buffer, gemini_url, url_len);
        size_t offset = url_len;
        io_buffer[offset++] = '?';
        
        // Out of the 1024 total bytes, 2 will be occupied by \r\n
        offset += input_callback(io_buffer + offset, meta, 1021 - offset);
        io_buffer[offset] = 0;

        // A new connection is presumably required
        // I tried using the already existing one but the server would not accept my input,
        // nor would it send any data back
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(connection);

        return gemini_fetch_document(ctx, io_buffer, input_callback);
        
    case '3':
        // If the server has requested a redirection, recursively call this function
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(connection);

        return gemini_fetch_document(ctx, meta, input_callback);

    case '4':
        // Identical requests may succeed in the future, so the user can retry
        error = GEMINI_TEMPORARY_FAILURE;
        goto fetch_failed;

    case '5':
        // Something is seriously wrong with the server
        error = GEMINI_PERMANENT_FAILURE;
        goto fetch_failed;

    case '6':
        // As of now, I'm considering this an error
        // The program cannot currently handle client certificates
        error = GEMINI_CLIENT_CERTIFICATE_REQUIRED;
        goto fetch_failed;
    }

fetch_failed:
    gemini_document_t *document = malloc(sizeof(gemini_document_t));
    document->error = error;
    document->url = strdup(gemini_url);
    
    // If the status starts with a two, fetch the content
    if (status[0] == '2' && document->error == GEMINI_OK)
    {
        // If the result is text, collect its content
        // If <META> is an empty string, text/gemini is assumed
        bool is_gemini = (header_length < 6 || !strncmp(meta, "text/gemini", 9));
        bool is_text = !strncmp(meta, "text", 4);

        if (is_text)
        {
            gemini_document_collect_content(document, ssl);

            if (is_gemini)
                gemini_document_parse_gemtext(document);
            else
                // If it's text but not gemtext, just handle it like a large preformatted block!
                gemini_document_parse_text(document);
        }
        else
        {
            document->error = GEMINI_NOT_TEXT;
        }
    }
        
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(connection);

    return document;
}

void gemini_document_destroy(gemini_document_t *document)
{
    free(document->url);
    dyn_array_destroy(document->content);
    dyn_array_destroy(document->elements);
}
