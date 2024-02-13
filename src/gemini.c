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
static int create_ordinary_tcp_connection(const char *hostname, gemini_status_e *status)
{
    // Creating a TCP connection
    int connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket < 0)
        exit_with_failure("failed to initialize TCP socket");

    struct addrinfo dns_hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };
    
    // Collecting the IP address of the server
    // A linked list will be return resulting from DNS lookup process
    struct addrinfo *server_info;
    
    if (getaddrinfo(hostname, "1965", &dns_hints, &server_info) != 0)
    {
        *status = GEMINI_IP_RESOLVE_FAILURE;
        return -1;
    }

    // I'm only going to be dealing with IPv4 addresses as of now
    if (server_info->ai_family != AF_INET)
        exit_with_failure("please provide an IPv4 server");

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
        // Collect the level of the heading and preventing an out-of-bounds runtime error
        if (line[1] && line[2] == '#') return GEMTEXT_HEADING_THREE;
        if (line[1] == '#') return GEMTEXT_HEADING_TWO;

        return GEMTEXT_HEADING_ONE;
    }

    if (!strncmp("=>", line, 2))  return GEMTEXT_LINK;
    if (!strncmp("```", line, 3)) return GEMTEXT_PREFORMATTED;

    // If nothing special was recognized, it must be a plain paragraph
    return GEMTEXT_PARAGRAPH;
}

void gemini_document_parse_content(gemini_document_t *document)
{
    // Parsing each line of the output into an array of gemtext elements
    document->elements = dyn_array_create(20, sizeof(gemtext_line_t));
    
    size_t offset = 0;
    size_t content_length = DYN_ARRAY_LENGTH(document->content);
    bool has_entered_preformatted = false;
    
    while (offset < content_length)
    {
        // Skip all empty new line characters if we are not inside a preformatted block
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

        // If it's just a new line character, don't go on the negatives. This case needs special treatment
        if (new_item->end == new_item->start)
            offset = new_item->end + 1;
        else
        {
            new_item->end--;
            offset = new_item->end + 2;
        }
    }
}

void gemini_document_collect_content(gemini_document_t *document, SSL *ssl)
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
}

gemini_document_t* gemini_fetch_document(SSL_CTX *ctx, char *gemini_url)
{
    gemini_document_t *document = malloc(sizeof(gemini_document_t));
    document->url = strdup(gemini_url);

    char *hostname = get_hostname_with_scheme(gemini_url);
    int connection = create_ordinary_tcp_connection(hostname + 9, &document->status);
    free(hostname);

    // Quit early if an error was encountered during the simple socket connection
    if (document->status != GEMINI_OK)
        return document;
        
    // Create a new TLS connection using the provided context
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, connection);

    if (SSL_connect(ssl) != 1)
    {
        document->status = GEMINI_TLS_HANDSHAKE_FAILURE;
        goto fetch_failed;
    }

    // The client needs to request a gemini page from the server.
    // The limit is extracted from the specification. A scheme should also be included.
    // The requset shall be terminated with a (Carriage Return + New Line Feed) sequence
    char io_buffer[1024 + 3];
    sprintf(io_buffer, "%s\r\n", gemini_url);
    SSL_write(ssl, io_buffer, strlen(io_buffer));

    // Read and parse the server's response header
    size_t header_length = 0;

    do
    {
        // Continue reading until a new line character has been received
        int bytes_read = SSL_read(ssl, io_buffer + header_length, sizeof(io_buffer));
        if (bytes_read <= 0)
            break;
        
        header_length += bytes_read;
        
    } while (io_buffer[header_length - 1] != '\n');

    // If not even a status was received, something went wrong
    if (header_length < 4)
    {
        document->status = GEMINI_HEADER_PARSING_FAILURE;
        goto fetch_failed;
    }

    // Insert a NULL byte and parse into separate strings
    io_buffer[header_length] = 0;
    char status[2];
    char meta[1024];
    sscanf(io_buffer, "%2s %s\r\n", status, meta);
    
    switch (status[0])
    {
    case '1':
        // The server is asking for input
        exit_with_failure("asking for input: %s", meta);
        return NULL;
        
    case '3':
        // If the server has requested a redirection, recursively call this function
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(connection);

        // Do something in the case that the resource is not a gemini URL
        return gemini_fetch_document(ctx, meta);

    case '4':
        // Identical requests may succeed in the future, so the user can retry
        document->status = GEMINI_TEMPORARY_FAILURE;
        goto fetch_failed;

    case '5':
        // Something is seriously wrong with the server
        document->status = GEMINI_PERMANENT_FAILURE;
        goto fetch_failed;

    case '6':
        // As of now, I'm considering this an error
        // The program cannot currently handle client certificates
        document->status = GEMINI_CLIENT_CERTIFICATE_REQUIRED;
        goto fetch_failed;
    }

    // If the status starts with a two, fetch the content
    // If <META> is an empty string, text/gemini is assumed
    if (header_length < 6 || !strncmp(meta, "text/gemini", 9))
    {
        gemini_document_collect_content(document, ssl);
        gemini_document_parse_content(document);
        document->status = GEMINI_OK;
    }
    else
    {
        document->status = GEMINI_NOT_GEMTEXT;
        goto fetch_failed;
    }
        
fetch_failed:
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
