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
    // This structure will manage all gemtext documents
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

// Returns the current y_offset of the window
// Will append another line feed so future paragraphs can be inserted properly
static int print_text_with_word_breaks(size_t y_offset, char *buffer, size_t length)
{
    size_t index = 0;
    size_t x_offset = 0;
    size_t width = getmaxx(globals.document_viewer);
    
    for (;;)
    {
        // Get the boundary of the next word
        size_t word_end = index;
        while (word_end < length && !isspace(buffer[word_end])) word_end++;

        // Check if the word fits on the current line
        if (x_offset + (word_end - index + 1) > width)
        {
            x_offset = 0;
            y_offset++;
        }
        
        mvwprintw(globals.document_viewer, y_offset, x_offset, "%.*s",
                  word_end - index + 1, buffer + index);

        x_offset += word_end - index + 1;

        // Check if the printing is done
        index = word_end + 1;
        if (index > length - 1)
            break;
    }

    return y_offset + 1;
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
        y_offset = print_text_with_word_breaks(y_offset, document->content + line->start, line->end - line->start + 1);
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
            // If this is the last link or list item on a chain, add some spacing at the bottom
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
    
    // Navigating to the specified starter page
    gemini_browser_load_document(&globals.browser, gemini_url);

    set_status("{browsing} %s", CURRENT_BROWSER_PAGE->document->url);
    refresh_document_viewer();
}

// Navigate to the link under the cursor
static void follow_link(void)
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
    // You may want to modify this command at the top of this file
    else if (link.scheme == LINK_SCHEME_HTTP || link.scheme == LINK_SCHEME_HTTPS)
    {
        char *command = join_strings_together(
            WEB_BROWSER_COMMAND, strlen(WEB_BROWSER_COMMAND),
            link.content, link.length + 1);

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

    // Refresh needs to be called here for some reason, even though the screen will be modified again
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

// Reads user input from the status bar
// Will save string into the buffer and return the total amount of bytes written
static size_t collect_user_input(char *buffer, char *prompt, size_t max_length)
{
    size_t input_length = 0;
    curs_set(1);
    
    // Print out the prompt to help the user out
    wclear(globals.status_bar);
    wprintw(globals.status_bar, "{%s}: ", prompt);
    wrefresh(globals.status_bar);
    
    int c;
    size_t offset = strlen(prompt) + 4;
    int width = getmaxx(globals.status_bar);

    wmove(globals.status_bar, 0, offset);

    while ((c = getch()) != '\n' || c == EXIT_KEY)
    {
        if (isprint(c) && offset < width)
        {
            // Spaces should be encoded
            if (c == ' ')
            {
                if (input_length + 3 < max_length - 1)
                {
                    strncpy(buffer + offset, "%20", 3);
                    mvwaddstr(globals.status_bar, 0, offset, "%20");
                    input_length += 3;
                    offset += 3;
                }
            }
            else
            {
                buffer[input_length] = c;
                input_length++;
                waddch(globals.status_bar, c);
                offset++;
            }
        }
        // If the backspace was pressed, move back and delete the last character
        else if (c == KEY_BACKSPACE && input_length > 0)
        {
            offset--;
            input_length--;
            mvwdelch(globals.status_bar, 0, offset);
        }

        // Move the cursor to the correct position
        wmove(globals.status_bar, 0, offset);
        wrefresh(globals.status_bar);
    }

    // Hide the cursor again
    curs_set(0);
    return input_length;
}

static void visit_page_of_prompt(void)
{
    globals.input_length = collect_user_input(globals.input_buffer, "insert gemini url", 1022);
    globals.input_buffer[globals.input_length] = 0;
    
    // Check if the user inserted a gemini scheme
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

int main(int argc, char **argv)
{
    // Validating user input
    if (argc != 2)
        exit_with_failure("please provide a gemini URL");

    if (strncmp(argv[1], "gemini://", 9))
        exit_with_failure("please provide a valid gemini:// url");

    gemini_browser_create(&globals.browser, collect_user_input);

    /*
     * The program's structure is flexible enough, so a variety of distinct frontends can be built without much hussle
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
    navigate_to_url(argv[1]);

    int c;
    while ((c = getch()) != EXIT_KEY)
    {
        gemini_page_t *page = CURRENT_BROWSER_PAGE;
        
        switch (c)
        {
        case GO_TO_START_KEY: scroll_to(0); break;
        case GO_TO_BOTTOM_KEY: scroll_to(DYN_ARRAY_LENGTH(page->document->elements) - 1); break;
        case MOVE_UP_KEY: scroll_to(page->scroll_offset - 1); break;
        case MOVE_DOWN_KEY: scroll_to(page->scroll_offset + 1); break;

        case PAGE_DOWN_KEY:
            scroll_to(MIN(page->scroll_offset + globals.total_elements_on_view - 1,
                          DYN_ARRAY_LENGTH(page->document->elements) - 1));
            break;
            
        case FOLLOW_LINK_KEY:
            follow_link();
            break;

        case SEARCH_KEY:
            navigate_to_url("gemini://geminispace.info/search");
            break;
            
        case GO_BACK_KEY:
            if (globals.browser.pages.length < 2) break;
            
            gemini_browser_go_back(&globals.browser);
            set_status("{browsing} %s", CURRENT_BROWSER_PAGE->document->url);
            refresh_document_viewer();
            break;

        case VISIT_PAGE_KEY:
            visit_page_of_prompt();
            break;
            
        case KEY_RESIZE:
            // Move and scale the UI accordingly to fit in with the new terminal dimensions
            handle_window_resize();
            break;
        }
    }

    endwin();
}
