#ifndef DISPLAY_CONSTANTS_H
#define DISPLAY_CONSTANTS_H

// Display dimension constants
const int SCREEN_WIDTH = 240;    // ST7735 width
const int SCREEN_HEIGHT = 135;   // ST7735 height

// Layout constants
const int MARGIN = 7;           // Side margins
const int BUBBLE_PADDING = 7;   // Padding inside message bubbles
const int CHAR_WIDTH = 12;        // Width of a single character
const int EMOJI_SIZE = 16;       // Width and height of emoji
const int EMOJI_PADDING = 4;     // Padding after emojis
const int LINE_HEIGHT = 18;      // Match emoji height
const int CHAR_HEIGHT = 16;     
const int CORNER_RADIUS = 5;    // Bubble corner radius

// Scroll-related constants
const int LINES_PER_SCREEN = 4;  // Maximum lines that fit on screen
const int MAX_LINES = 3;         // Maximum lines per chunk
const int ELLIPSIS_HEIGHT = 9;   // Height of the ellipsis dots

// Derived constants
const int EFFECTIVE_WIDTH = SCREEN_WIDTH - (MARGIN * 2);
const int CHARS_PER_LINE = (EFFECTIVE_WIDTH - (BUBBLE_PADDING * 2)) / CHAR_WIDTH;

// Additional constants
const int AVAILABLE_HEIGHT = SCREEN_HEIGHT - (2 * BUBBLE_PADDING) - ELLIPSIS_HEIGHT;

#endif // DISPLAY_CONSTANTS_H