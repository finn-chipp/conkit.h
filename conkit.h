/* Conkit -- cross-platform console-operation abstraction header
 * Copyright (C) 2023 Finn Chipp
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h> // For i/o
#include <stdlib.h> // For calloc(), free(), and size_t
#include <math.h> // For number of chars that size_t is to print
#include <string.h> // For strcat(), memset(), strlen() (also size_t lol)

// Non-implementation-specific definitions:

struct ck_console_size { // For console dimensions
    size_t width,
           height;
    _Bool has_changed; // Terminal was resized?
};

size_t CK_SCREEN_BUFFER_SIZE,
       CK_SCREEN_BUFFER_END;

char *CK_SCREEN_BUFFER, // Buffer to write to that will be written to screen
     *CK_SEQUENCE_BUFFER; // Buffer to store character sequences generated by ANSI abstraction-functions

void *CK_ALLOC_BUFFER; // Buffer for memory-reallocation in case of failure

#ifndef CK_ALLOC_SIZE
#define CK_ALLOC_SIZE 100
#endif

// Implementation-specific definitions:

#ifdef _WIN32 // Windows

#include <conio.h> // For getch(), kbhit()
#include <windows.h> // For GetConsoleScreenBufferInfo(), other Windows nonsense

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4
#endif

#define sleep(ms) Sleep(ms)

HANDLE CK_STD_OUTPUT_HANDLE;
DWORD CK_CONSOLE_MODE;

void ck_init(void) { // Initialise ck
    // Enable ANSI escape sequences ("VIRTUAL_TERMINAL_PROCESSING"):

    CK_STD_OUTPUT_HANDLE = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(CK_STD_OUTPUT_HANDLE, &CK_CONSOLE_MODE);
    SetConsoleMode(CK_STD_OUTPUT_HANDLE, CK_CONSOLE_MODE | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    // Allocate memory for CK_SCREEN_BUFFER:
    
    if((CK_SCREEN_BUFFER = calloc(CK_SCREEN_BUFFER_SIZE = CK_ALLOC_SIZE, sizeof(char))) == NULL) {
        perror("Error allocating memory for CK_SCREEN_BUFFER: ");
        exit(EXIT_FAILURE);
    }

    // Initialise CK_SCREEN_BUFFER:
    
    CK_SCREEN_BUFFER_END = 0;
    memset(CK_SCREEN_BUFFER, '\0', CK_ALLOC_SIZE);
    
    // Allocate memory for CK_SEQUENCE_BUFFER:
    
    if((CK_SEQUENCE_BUFFER = calloc(2 * (log10(pow(2, sizeof(size_t) * 8) - 1) + 1) + 21, sizeof(char))) == NULL) { // Max length of a size_t in digits for base-10, times two because for one of the functions there is two size_t needed, plus 20 because the rgb function requires a 20 character string, plus 1 for \0
        perror("Error allocating memory for CK_SEQUENCE_BUFFER: ");
        exit(EXIT_FAILURE);
    }
}

void ck_end(void) { // End usage of ck and put things back to normal
    SetConsoleMode(CK_STD_OUTPUT_HANDLE, CK_CONSOLE_MODE); // Set console back to the way it was (don't leave them with escape codes enabled if that's how they were originally)

    // Free allocated memory:
    
    free(CK_SCREEN_BUFFER);
    free(CK_SEQUENCE_BUFFER);
}

struct ck_console_size ck_current_console_size(void) { // Get current terminal dimensions
    size_t newWidth, newHeight;

    static struct ck_console_size knownSize = {0, 0, 0};
    CONSOLE_SCREEN_BUFFER_INFO currentBufferInfo;

    // Get the info:
    
    GetConsoleScreenBufferInfo(CK_STD_OUTPUT_HANDLE, &currentBufferInfo);

    // Work out dimensions:
    
    newWidth = currentBufferInfo.srWindow.Right - currentBufferInfo.srWindow.Left + 1,
    newHeight = currentBufferInfo.srWindow.Bottom - currentBufferInfo.srWindow.Top + 1;

    // Modify return value if they differ from what's already known:
    
    if((knownSize.has_changed = newWidth != knownSize.width || newHeight != knownSize.height))
        knownSize.width = newWidth,
        knownSize.height = newHeight;

    return knownSize;
}

// Unix-like:

#elif defined(unix) || \
      defined(__unix) || \
      defined(__unix__) || \
      defined(__APPLE__) || \
      defined(__MACH__)

#include <termios.h> // For disabling input echo and buffering
#include <unistd.h> // For sleep (and some other stuff I think, I can't remember)
#include <poll.h> // To poll stdin for my implementation of kbhit()
#include <sys/ioctl.h> // To get terminal dimensions

#define sleep(ms) usleep(ms * 1000)

struct termios CK_CONSOLE_SETTS,
               CK_CONSOLE_ORIG_SETTS;

char getch(void) { // My implementation of the getch() function
    char ret;

    // Change settings, get char, revert settings, return value:
    
    tcsetattr(0, TCSANOW, &CK_CONSOLE_SETTS);

    ret = getchar();

    tcsetattr(0, TCSANOW, &CK_CONSOLE_ORIG_SETTS);

    return ret;
}

_Bool kbhit(void) { // My own implementation of the kbhit() function
    static struct pollfd fd_buff[] = {{.fd = STDIN_FILENO, // File descriptor for stdin
                                       .events = POLLIN}};

    tcsetattr(0, TCSANOW, &CK_CONSOLE_SETTS); // Set non-echo, non-buffering settings

    poll(fd_buff, 1, 0); // Poll the items in the file descriptor array, waiting 0s for each (since kbhit() will be called to check if the keyboard has *been* hit, not to wait for it to *be* hit)

    tcsetattr(0, TCSANOW, &CK_CONSOLE_ORIG_SETTS); // Revert settings

    return fd_buff[0].revents & POLLIN; // So, was there input?
}

void ck_init(void) { // Initialise ck
    // Pre-compute unechoed-and-unbuffered-input attributes (and original attributes) so they can be readily applied:

    tcgetattr(0, &CK_CONSOLE_ORIG_SETTS);
    CK_CONSOLE_SETTS = CK_CONSOLE_ORIG_SETTS;

    CK_CONSOLE_SETTS.c_lflag &= ~ECHO & ~ICANON;

    // Allocate memory for CK_SCREEN_BUFFER:

    if((CK_SCREEN_BUFFER = calloc(CK_SCREEN_BUFFER_SIZE = CK_ALLOC_SIZE, sizeof(char))) == NULL) {
        perror("Error allocating memory for CK_SCREEN_BUFFER: ");
        exit(EXIT_FAILURE);
    }

    // Initialise CK_SCREEN_BUFFER:
    
    CK_SCREEN_BUFFER_END = 0;
    memset(CK_SCREEN_BUFFER, '\0', CK_ALLOC_SIZE);
    
    // Allocate memory for CK_SEQUENCE_BUFFER:
    
    if((CK_SEQUENCE_BUFFER = calloc(2 * (log10(pow(2, sizeof(size_t) * 8) - 1) + 1) + 21, sizeof(char))) == NULL) { // Max length of a size_t in digits for base-10, times two because for one of the functions there is two size_t needed, plus 20 because the rgb function requires a 20 character string, plus 1 for \0
        perror("Error allocating memory for CK_SEQUENCE_BUFFER: ");
        exit(EXIT_FAILURE);
    }
}

void ck_end(void) { // End usage of ck and put things back to normal
    // Free allocated memory:

    free(CK_SCREEN_BUFFER);
    free(CK_SEQUENCE_BUFFER);
}

struct ck_console_size ck_current_console_size(void) { // Get current terminal dimensions
    struct winsize newDims;

    static struct ck_console_size knownSize = {0, 0, 0};
    
    // Get the dimensions:
    
    ioctl(0, TIOCGWINSZ, &newDims);
    
    // Modify return value if they differ from what's already known:
    
    if((knownSize.has_changed = newDims.ws_col != knownSize.width || newDims.ws_row != knownSize.height))
        knownSize.width = newDims.ws_col,
        knownSize.height = newDims.ws_row;

    return knownSize;
}

#else // RIP (maybe support later)

#error Could not be determined if system is Unix-like or Windows! I do not know how I should be implemented! Please `#define unix', or `#define _WIN32'.

#endif

// Non-implementation-specific function definitions:

// Versions for non-literal arguments:

/* Note: since these functions write to the same
 * `CK_SEQUENCE_BUFFER', each call will overwrite
 * the buffer used by the previous. This is not a
 * problem most of the time, but might be in some
 * scenarios, such as using multiple of them as
 * arguments in a call to printf(), wherein each
 * successive call to any of them will overwrite
 * the generated string of the last, and that will
 * be the value that is used for both of them when
 * the string is ultimately printed, since they
 * all get evaluated before being passed in.
 * Usage of something like strdup() may be advised.
 */

// Note: this is not a problem for the literal-argument versions, since they do not require a buffer be written-to

#define CK_CLEAR_CONSOLE "\033[2J"
#define CK_RESET_FORMATTING "\033[0m"
#define CK_SHOW_CURSOR "\033[?25h"
#define CK_HIDE_CURSOR "\033[?25l"

char *ck_rgb(unsigned char where, // Generate ANSI escape-sequence string for specified RGB value
             unsigned char r,
             unsigned char g,
             unsigned char b) {

    sprintf(CK_SEQUENCE_BUFFER,
            "\033[%d;2;%d;%d;%dm",
            where,
            r, g, b);
    
    return CK_SEQUENCE_BUFFER;
}

#define ck_bg_rgb(r, g, b) ck_rgb(48, (r), (g), (b)) /* Background */
#define ck_fg_rgb(r, g, b) ck_rgb(38, (r), (g), (b)) /* Foreground */

char *ck_cursor_goto(size_t x, size_t y) { // Generate ANSI escape-sequence string for cursor movement to specified co-ordinates
    sprintf(CK_SEQUENCE_BUFFER, "\033[%zu;%zuH", y, x);

    return CK_SEQUENCE_BUFFER;
}

char *ck_cursor_move(char where, size_t amount) { // Generate ANSI escape-sequence string for cursor movement in a direction relative-to current position
    sprintf(CK_SEQUENCE_BUFFER, "\033[%zu%c", amount, where);

    return CK_SEQUENCE_BUFFER;
}

#define ck_cursor_up(amount) ck_cursor_move('A', (amount)) /* move cursor upward by `amount' lines */
#define ck_cursor_down(amount) ck_cursor_move('B', (amount)) /* Move cursor downward by `amount' lines */
#define ck_cursor_right(amount) ck_cursor_move('C', (amount)) /* Move cursor rightward by `amount' chars */
#define ck_cursor_left(amount) ck_cursor_move('D', (amount)) /* Move cursor leftward by `amount' chars */

// Versions that should be used when the specified arguments are literals:

#define ck_bg_rgb_l(r, g, b) "\033[48;2;" #r ";" #g ";" #b "m" /* Background RGB value */
#define ck_fg_rgb_l(r, g, b) "\033[38;2;" #r ";" #g ";" #b "m" /* Foreground RGB value */

#define ck_cursor_goto_l(x, y) "\033[" #y ";" #x "H" /* Cursor movement to specified co-ordinates */

#define ck_cursor_up_l(amount) "\033[" #amount "A" /* move cursor upward by `amount' lines */
#define ck_cursor_down_l(amount) "\033[" #amount "B" /* Move cursor downward by `amount' lines */
#define ck_cursor_right_l(amount) "\033[" #amount "C" /* Move cursor rightward by `amount' chars */
#define ck_cursor_left_l(amount) "\033[" #amount "D" /* Move cursor leftward by `amount' chars */

// Other functions:

void ck_print(char *buffer) { // Write a string to the CK_SCREEN_BUFFER
    size_t len = strlen(buffer) + 1;

    // Automatic reallocation if needed:

    while(CK_SCREEN_BUFFER_END + len + 1 > CK_SCREEN_BUFFER_SIZE) // Calculate new size
        CK_SCREEN_BUFFER_SIZE += CK_ALLOC_SIZE;
    
    CK_SCREEN_BUFFER_END += len + 1;
    
    if((CK_ALLOC_BUFFER = (void *)realloc(CK_SCREEN_BUFFER, CK_SCREEN_BUFFER_SIZE * sizeof(char))) == NULL) { // Reallocate
        perror("Error reallocating memory for CK_SCREEN_BUFFER: ");

        free(CK_SCREEN_BUFFER);
        free(CK_SEQUENCE_BUFFER);

        exit(EXIT_FAILURE);
    }

    CK_SCREEN_BUFFER = (char *)CK_ALLOC_BUFFER;
    
    strcat(CK_SCREEN_BUFFER, buffer);
}

void ck_flip(void) { // Print the contents of CK_SCREEN_BUFFER and subsequently clear it
    printf("\033[H%s", CK_SCREEN_BUFFER);

    memset(CK_SCREEN_BUFFER, '\0', CK_SCREEN_BUFFER_SIZE);
    CK_SCREEN_BUFFER_END = 0;
}
