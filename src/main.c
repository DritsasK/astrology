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

#include <stdio.h>
#include <ncurses.h>
#include <unistd.h>
#include <ctype.h>
#include "common.h"
#include "gemini.h"
#include "browser.h"
#include "config.h"
#include "dynamic_array.h"

enum
{
    COLOR_LINK = 1,
    COLOR_LIST_ITEM
};

struct
{
    // This structure will manage all TLS connections and parse the data
    // This file will only be dedicated to rendering that data onto the screen
    gemini_browser_t browser;

    WINDOW *status_bar;
    char input_buffer[1024];
    size_t input_length;
    
    WINDOW *document_viewer;
    int total_elements_on_view;
} globals;

#define CURRENT_BROWSER_PAGE ((gemini_page_t*) globals.browser.pages.head->data)

static void set_status(const char *format, ...)
{
    // Clear the previous value
    wclear(globals.status_bar);
    
    va_list args;
    va_start(args, format);

    wmove(globals.status_bar, 0, 0);
    vw_printw(globals.status_bar, format, args);
    wrefresh(globals.status_bar);

    va_end(args);
}

static int get_element_type_attributes(gemtext_line_e type)
{
    switch (type)
    {
    case GEMTEXT_HEADING_ONE:
    case GEMTEXT_HEADING_TWO:
    case GEMTEXT_HEADING_THREE:
        return A_BOLD;
        
    case GEMTEXT_LINK: return A_ITALIC | COLOR_PAIR(COLOR_LINK);
    case GEMTEXT_LIST_ITEM: return COLOR_PAIR(COLOR_LIST_ITEM);
    case GEMTEXT_BLOCKQUOTE: return A_DIM;

    default:
        // Everything else will just show up as normal text
        return A_NORMAL;
    }
}

// Returns the new y_offset of the window after the given xtext buffer has been appended
static int print_text_with_word_breaks(size_t y_offset, char *buffer, size_t length)
{
    size_t buffer_index = 0;
    size_t x_offset = 0;
    size_t width = getmaxx(globals.document_viewer);
    
    for (;;)
    {
        // Get the boundary of the next word
        size_t word_end = buffer_index;
        while (word_end < length && !isspace(buffer[word_end])) word_end++;

        // Check if the word fits on the current line
        if (x_offset + (word_end - buffer_index + 1) > width)
        {
            x_offset = 0;
            y_offset++;
        }
        
        mvwprintw(globals.document_viewer, y_offset, x_offset, "%.*s",
                  word_end - buffer_index + 1, buffer + buffer_index);

        x_offset += word_end - buffer_index + 1;

        // Check if the printing is done
        buffer_index = word_end + 1;
        if (buffer_index > length - 1)
            break;
    }

    return y_offset;
}

static void refresh_document_viewer(void)
{
    wclear(globals.document_viewer);
    globals.total_elements_on_view = 0;
    
    // Get whatever ends first, either the screen's limit or the remaining elements
    gemini_page_t *page = CURRENT_BROWSER_PAGE;
    gemini_document_t *document = page->document;
    size_t y_offset = 0;
    
    for (size_t i = page->scroll_offset; i < DYN_ARRAY_LENGTH(document->elements); i++)
    {
        // Check if we've reached the bottom of the screen
        if (y_offset >= getmaxy(globals.document_viewer) - 1)
            break;

        gemtext_line_t *line = &document->elements[i];
        // Wrap each element on an attribute based on its type
        int attrs = get_element_type_attributes(line->type);
        
        wattron(globals.document_viewer, attrs);
        // Add one to the offset because each gemini element represents a separate line
        y_offset = print_text_with_word_breaks(y_offset, document->content + line->start, line->end - line->start + 1) + 1;
        wattroff(globals.document_viewer, attrs);

        // Some elements require some extra spacing to improve readability
        switch (line->type)
        {
        case GEMTEXT_PARAGRAPH:
        case GEMTEXT_HEADING_ONE:
        case GEMTEXT_HEADING_TWO:
        case GEMTEXT_HEADING_THREE:
        case GEMTEXT_BLOCKQUOTE:
            y_offset++;
            break;

        case GEMTEXT_LINK:
        case GEMTEXT_LIST_ITEM:
        case GEMTEXT_PREFORMATTED:
            // If this is the element of a chain, add some spacing at the bottom
            if (i != DYN_ARRAY_LENGTH(document->elements) - 1 &&
                document->elements[i + 1].type != line->type)
            {
                y_offset++;
            }
            
            break;
        }

        globals.total_elements_on_view++;
    }

    wrefresh(globals.document_viewer);
}

// Updates the status bar and navigates to the specified gemini url
static void navigate_to_url(char *gemini_url)
{
    set_status("{loading}: connecting to %s", gemini_url);
    gemini_browser_load_document(&globals.browser, gemini_url);

    set_status("{browsing} %s", CURRENT_BROWSER_PAGE->document->url);
    refresh_document_viewer();
}

static void follow_link_under_cursor(void)
{
    browser_link_t link;
    gemini_browser_get_link_under_cursor(&globals.browser, &link);

    if (link.scheme == LINK_SCHEME_INVALID)
        return;

    // If it's a gemini site, just follow the link
    if (link.scheme == LINK_SCHEME_GEMINI)
    {
        navigate_to_url(link.content);
    }
    // If it's a webpage, open it up with the default browser
    // You may want to modify this command via the config.h file
    else if (link.scheme == LINK_SCHEME_HTTP || link.scheme == LINK_SCHEME_HTTPS)
    {
        char *command = join_strings_together(
            WEB_BROWSER_COMMAND, strlen(WEB_BROWSER_COMMAND),
            link.content, strlen(link.content));

        system(command);
        free(command);
    }

    browser_link_destroy(&link);
}

static void handle_window_resize(void)
{
    int screen_height, screen_width;
    getmaxyx(stdscr, screen_height, screen_width);

    // This code is almost identical to the window creation snippet
    int new_x = VIEWER_WIDTH < screen_width ? (screen_width - VIEWER_WIDTH) / 2 : 0;
    int new_width = MIN(screen_width, VIEWER_WIDTH);
    int old_y, old_x;
    getyx(globals.document_viewer, old_y, old_x);

    // It's fine if this call fails from time to time.
    // I shouldn't be terminating the whole program on error, right?
    wresize(globals.document_viewer, screen_height - 2, new_width);
    wresize(globals.status_bar, 1, new_width);

    if (old_x != new_x)
    {
        // Clear the old window so that no artifacts remain
        wclear(globals.document_viewer);
        wrefresh(globals.document_viewer);
            
        wclear(globals.status_bar);
        wrefresh(globals.status_bar);
        
        mvwin(globals.document_viewer, 2, new_x);
        mvwin(globals.status_bar, 0, new_x);
     }

    // Refresh needs to be called here, even though the screen will be modified again
    // Otherwise, the window disappears when scaling it down
    refresh();

    set_status("{browsing} %s", CURRENT_BROWSER_PAGE->document->url);
    refresh_document_viewer();
}

static void scroll_to(int new_offset)
{
    gemini_page_t *page = CURRENT_BROWSER_PAGE;
    
    // Only scroll if something has changed and we are still inside the valid bounds
    if (new_offset == page->scroll_offset) return;
    if (new_offset < 0 || new_offset > DYN_ARRAY_LENGTH(page->document->elements) - 1) return;

    page->scroll_offset = new_offset;
    refresh_document_viewer();
}

/*
 * Reads URL input using the status bar as a text box. Special characters will be encoded properly
 * Will save string into the buffer and return the total amount of bytes written
 * WARNING: I want to make this generic enough, so no NULL byte will be included
 */
static size_t collect_url_from_user(char *buffer, char *prompt, size_t max_length)
{
    size_t input_length = 0;
    
    wclear(globals.status_bar);
    wprintw(globals.status_bar, "{%s}: ", prompt);
    wrefresh(globals.status_bar);
    
    size_t offset_x = strlen(prompt) + 4;
    size_t scroll_x = 0;
    size_t max_visible_length = getmaxx(globals.status_bar) - offset_x;

    int c;
    while ((c = getch()) != '\n')
    {
        if (c == KEY_BACKSPACE && input_length > 0)
        {
            mvwdelch(globals.status_bar, 0, offset_x + MIN(input_length, max_visible_length) - 1);
            input_length--;
        }
        else if (isprint(c))
        {
            if (input_length >= max_length) continue;
            
            // Spaces should be encoded
            if (c == ' ')
            {
                if (input_length + 3 < max_length)
                {
                    strncpy(buffer + input_length, "%20", 3);
                    input_length += 3;
                }
            }
            else
            {
                buffer[input_length] = c;
                input_length++;
            }
        }

        // If we've gone past the bar's width, adjust the horizontal scroll
        if (input_length >= max_visible_length)
        {
            scroll_x = input_length - max_visible_length;
        }

        // Reprint the portion of the string that is visible
        mvwprintw(globals.status_bar, 0, offset_x, "%.*s",
                  MIN(max_visible_length, input_length), buffer + scroll_x);
        
        wrefresh(globals.status_bar);
    }

    return input_length;
}

static void visit_page_of_prompt(void)
{
    globals.input_length = collect_url_from_user(globals.input_buffer, "insert gemini url", 900);
    globals.input_buffer[globals.input_length] = 0;
    
    // Check if the user included a gemini scheme
    if (!strncmp(globals.input_buffer, "gemini://", 9))
    {
        navigate_to_url(globals.input_buffer);
    }
    else
    {
        // Otherwise, stick the gemini scheme into the URL manually
        char *new_url = join_strings_together(
            "gemini://", 9,
            globals.input_buffer, globals.input_length);

        navigate_to_url(new_url);
        free(new_url);
    }
}

void scroll_to_next_link(void)
{
    size_t start = CURRENT_BROWSER_PAGE->scroll_offset;
    gemtext_line_t *elements = CURRENT_BROWSER_PAGE->document->elements;
    size_t index = start + 1;
    
    // Start with a forward search
    while (index != DYN_ARRAY_LENGTH(elements) && elements[index].type != GEMTEXT_LINK) index++;
    
    // If nothing was found, wrap around from the start of the document
    if (index == DYN_ARRAY_LENGTH(elements))
    {
        index = 0;
        while (index != start && elements[index].type != GEMTEXT_LINK) index++;
        
        // If neither that worked, quit
        if (index == start)
            return;
    }

    CURRENT_BROWSER_PAGE->scroll_offset = index;
    refresh_document_viewer();
}

void navigate_to_host(void)
{
    char *host = get_hostname_with_scheme(CURRENT_BROWSER_PAGE->document->url);
    navigate_to_url(host);
    free(host);
}

// Just a thin wrapper around the user input handler so that there is some extra feedback to the user
size_t on_server_input(char *buffer, char *prompt, size_t max_length)
{
    size_t bytes_written = collect_url_from_user(buffer, prompt, max_length);
    set_status("{loading} the server is handling your input");

    return bytes_written;
}

int main(int argc, char **argv)
{
    // Validating user input
    if (argc == 2 && (strncmp(argv[1], "gemini://", 9) || strlen(argv[1]) > 1022))
        exit_with_failure("please provide a valid and reasonably sized gemini:// url");

    gemini_browser_create(&globals.browser, on_server_input);

    /*
     * The program's structure is flexible enough, so a variety of distinct frontends can be built without much work
     * In this version, I will be implementing an ncurses wrapper
     */
    initscr();
    start_color();
    
    if (!has_colors())
        exit_with_failure("please use a terminal that supports color");

    // Assign colors based on the terminal's settings
    // If no custom colors are available, just use the defaults
    if (can_change_color())
    {
        init_color(COLOR_LINK, 350, 800, 1000);
        init_pair(COLOR_LINK, COLOR_LINK, COLOR_BLACK);
        init_color(COLOR_LIST_ITEM, 800, 800, 400);
        init_pair(COLOR_LIST_ITEM, COLOR_LIST_ITEM, COLOR_BLACK);
    }
    else
    {
        init_pair(COLOR_LINK, COLOR_BLUE, COLOR_BLACK);
        init_pair(COLOR_LIST_ITEM, COLOR_GREEN, COLOR_BLACK);
    }

    keypad(stdscr, true);
    cbreak();
    curs_set(0);
    noecho();

    int screen_height, screen_width;
    getmaxyx(stdscr, screen_height, screen_width);

    // Center the document viewer and declare some maximum width
    int viewer_x = VIEWER_WIDTH < screen_width ? (screen_width - VIEWER_WIDTH) / 2 : 0;
    int viewer_width = MIN(screen_width, VIEWER_WIDTH);

    globals.document_viewer = newwin(screen_height - 2, viewer_width, 2, viewer_x);
    globals.status_bar = newwin(1, viewer_width, 0, viewer_x);

    refresh();
    navigate_to_url((argc == 2) ? argv[1] : HOME_URL);

    int c;
    while ((c = getch()) != EXIT_KEY)
    {
        gemini_page_t *page = CURRENT_BROWSER_PAGE;

        switch (c)
        {
        case GO_TO_START_KEY: scroll_to(0); continue;
        case GO_TO_BOTTOM_KEY: scroll_to(DYN_ARRAY_LENGTH(page->document->elements) - 1); continue;
        case MOVE_UP_KEY: scroll_to(page->scroll_offset - 1); continue;
        case MOVE_DOWN_KEY: scroll_to(page->scroll_offset + 1); continue;

        case PAGE_DOWN_KEY:
            scroll_to(MIN(page->scroll_offset + globals.total_elements_on_view - 1,
                          DYN_ARRAY_LENGTH(page->document->elements) - 1));
            continue;
            
        case FOLLOW_LINK_KEY:
            follow_link_under_cursor();
            continue;

        case SEARCH_ENGINE_KEY:
            navigate_to_url("gemini://geminispace.info/search");
            continue;
            
        case GO_BACK_KEY:
            if (globals.browser.pages.length < 2) continue;
            
            gemini_browser_go_back(&globals.browser);
            set_status("{browsing} %s", CURRENT_BROWSER_PAGE->document->url);
            refresh_document_viewer();
            continue;

        case VISIT_PAGE_KEY:
            visit_page_of_prompt();
            continue;

        case NEXT_LINK_KEY:
            scroll_to_next_link();
            continue;

        case GO_TO_HOST_KEY:
            navigate_to_host();
            continue;
            
        case KEY_RESIZE:
            // Move and scale the UI accordingly to fit in with the new terminal dimensions
            handle_window_resize();
            continue;
        }

        // Check if the user is trying to access one of the bookmarks
        if (c >= '1' && c <= '9')
        {
            if (globals.browser.bookmarks[c - '1'][0])
                navigate_to_url(globals.browser.bookmarks[c - '1']);
            else
                set_status("{error} the specified bookmark slot has not been set");
        }
        // Check if the user is trying to update a bookmark
        else
        {
            char *url = CURRENT_BROWSER_PAGE->document->url;
            if (!url) continue;
            
            static char bookmark_update_bindings[9] = "!@#$%^&*(";
            
            for (int i = 0; i < 9; i++)
            {
                if (c == bookmark_update_bindings[i])
                {
                    strcpy(globals.browser.bookmarks[i], url);
                    set_status("{update} successfully updated bookmark slot!");
                    continue;
                }
            }
        }
    }

    browser_destroy(&globals.browser);
    endwin();
}
