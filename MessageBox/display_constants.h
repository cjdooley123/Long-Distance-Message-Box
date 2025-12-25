#ifndef DISPLAY_CONSTANTS_H
#define DISPLAY_CONSTANTS_H

// Display dimension constants
const int SCREEN_WIDTH = 160;    // ST7735 width
const int SCREEN_HEIGHT = 128;   // ST7735 height

// Layout constants
const int MARGIN = 10;           // Side margins
const int BUBBLE_PADDING = 10;   // Padding inside message bubbles
const int CHAR_WIDTH = 6;        // Width of a single character
const int EMOJI_SIZE = 16;       // Width and height of emoji
const int EMOJI_PADDING = 4;     // Padding after emojis
const int LINE_HEIGHT = 16;      // Match emoji height
const int CORNER_RADIUS = 10;    // Bubble corner radius

// Scroll-related constants
const int LINES_PER_SCREEN = 6;  // Maximum lines that fit on screen
const int MAX_LINES = 5;         // Maximum lines per chunk
const int ELLIPSIS_HEIGHT = 8;   // Height of the ellipsis dots
const int FRACTION_MARGIN = 5;   // Margin for page fraction display

// Derived constants
const int EFFECTIVE_WIDTH = SCREEN_WIDTH - (MARGIN * 2);
const int CHARS_PER_LINE = (EFFECTIVE_WIDTH - (BUBBLE_PADDING * 2)) / CHAR_WIDTH;


// Additional constants
const int AVAILABLE_HEIGHT = SCREEN_HEIGHT - (2 * BUBBLE_PADDING) - ELLIPSIS_HEIGHT;

#endif // DISPLAY_CONSTANTS_H