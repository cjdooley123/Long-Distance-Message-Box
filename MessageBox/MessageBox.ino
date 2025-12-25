//#define FIREBASE_ARDUINO_USE_EXTERNAL_CLIENT
//#define ESP_MAIL_USE_EXTERNAL_CLIENT

#include <Preferences.h>

// At global scope
Preferences preferences;

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_LSM6DSOX.h>

#include <FirebaseClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>

#include "message_variations.h"
#include "emoji_definitions.h"
#include "display_constants.h"

using namespace firebase;

// Pin Definitions
#define TFT_CS    10
#define TFT_RST   8
#define TFT_DC    9
#define TFT_MOSI  11
#define TFT_SCLK  13
#define BUZZER_PIN A4
#define TFT_BL   12  // Backlight control pin

// At beginning of file with other constants:
// At beginning of file with other constants:
const int NOTE_FS5 = 740;   // F#5 = 739.99 Hz
const int NOTE_A5 = 880;    // A5 = 880.00 Hz
const int NOTE_F6 = 1397;
const int NOTE_G6 = 1568;   // G6 = 1567.98 Hz
const int NOTE_GS6 = 1661;
const int NOTE_AS6 = 1865;  // A#6 = 1864.66 Hz
const int NOTE_B6 = 1976;


// Configuration Constants
const unsigned long SHAKE_CHECK_INTERVAL = 100;    // Check for shake every 100ms
const unsigned long DB_CHECK_INTERVAL = 30000;     // Check database every minute
const unsigned long SLEEP_TIMEOUT = 15000;         // 10 seconds in milliseconds
const unsigned long SCROLL_DELAY = 4000;           // 1 second per line
const float SHAKE_THRESHOLD = 20.0;                // Threshold for shake detection
const int MAX_MESSAGE_LENGTH = 1000;                // Maximum message length

// WiFi and Email Configuration
const char* WIFI_SSID = "YOUR_WIFI_NETWORK";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* DATABASE_URL = "https://YOUR_FIREBASE_URL.firebaseio.com";

// Constants for timezone conversion
const int EST_OFFSET = -5;  // East Coast Time is UTC-5 (EST)
const int SECONDS_PER_HOUR = 3600;

// Global Objects
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
Adafruit_LSM6DSOX imu;
//SMTPSession smtp;

// Firebase Objects
DefaultNetwork network;
NoAuth noAuth;
FirebaseApp app;
WiFiClientSecure ssl_client;
AsyncClientClass aClient(ssl_client, getNetwork(network));
RealtimeDatabase Database;
AsyncResult aResult_no_callback;

// Global State Variables
enum SystemState {
    INITIALIZING,
    OPEN,
    SLEEP,
    UNOPEN
};

struct ScrollState {
    uint8_t currentChunk;
    uint8_t totalChunks;
    unsigned long lastScrollTime;
    bool hasCompletedOnePass;
    bool isInitialized;
    String displayChunks[100];  // or whatever max you need
} scrollState;

SystemState currentState = INITIALIZING;
bool isFirstEntryOpen = true;
bool isFirstEntrySleep = true;
bool isFirstEntryUnopen = true;
String currentMessage = "Waiting for new messages...";
String lastMessageId = "";
String currentTimestamp = "";
bool isScrolling = false;
bool hasUnreadMessage = false;
unsigned long lastShakeTime = 0;
unsigned long lastDBCheck = 0;
unsigned long lastActivityTime = 0;
int screenBrightness = 255;

// Error Handling
void handleError(const String& error) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_RED);
    tft.println("Error:");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(error);
    
    drawEmoji(&tft, EMOJI_SAD, tft.width() - 20, 0);
    delay(5000);
    
    if (WiFi.status() != WL_CONNECTED) {
       setupWiFi();
    }
}

void printError(int code, const String &msg) {
    Serial.printf("Error, msg: %s, code: %d\n", msg.c_str(), code);
}

// Setup Functions
void setupDisplay() {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(true);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
}

void setupIMU() {
    pinMode(6, OUTPUT);
    digitalWrite(6, HIGH);
    
    tft.println("\nStarting I2C initialization...");
    Wire.begin(3, 4);
    delay(100);  // Give I2C time to stabilize

    Serial.println("Scanning for I2C devices...");
    for (byte address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      byte error = Wire.endTransmission();
      if (error == 0) {
        Serial.print("I2C device found at address 0x");
        if (address < 16) {
          Serial.print("0");
        }
        Serial.println(address, HEX);
      
        // If this is our LSM6DSOX address, try reading WHO_AM_I register
        if (address == 0x6A) {
          Wire.beginTransmission(0x6A);
          Wire.write(0x0F);  // WHO_AM_I register address
          Wire.endTransmission(false);
          Wire.requestFrom(0x6A, 1);
          if (Wire.available()) {
            byte whoAmI = Wire.read();
            Serial.print("WHO_AM_I register value: 0x");
            Serial.println(whoAmI, HEX);
          // Should be 0x6C for LSM6DSOX
          }
        }
      }
    }

    Serial.println("Attempting to initialize LSM6DSOX...");
    if (!imu.begin_I2C()) {
        handleError("IMU initialization failed!");
        Serial.println("Failed to find LSM6DSOX chip");
    }
    Serial.println("LSM6DSOX Found!");

    tft.println("\nLSM6DSOX Found!");

    imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
    imu.setGyroRange(LSM6DS_GYRO_RANGE_250_DPS);
    imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
    imu.setGyroDataRate(LSM6DS_RATE_104_HZ);
}

void setupWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.println("Connecting to WiFi...");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        tft.print(".");
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        handleError("WiFi connection failed!");
    }
}

void setupFirebase() {
    #if defined(ESP32)
        ssl_client.setInsecure();
    #endif
    
    initializeApp(aClient, app, getAuth(noAuth), aResult_no_callback);
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    aClient.setAsyncResult(aResult_no_callback);
    
    if (aClient.lastError().code() == 0) {
        tft.println("\nFirebase connected!");
    } else {
        handleError("Firebase setup failed!");
    }
}

void fadeDisplay() {
    tft.fillScreen(ST77XX_BLACK);
}

void wakeDisplay() {
    tft.fillScreen(ST77XX_BLACK);
    lastActivityTime = millis();
}

void calculateTextDimensions(const String& text, int& width, int& emojiCount) {
    width = 0;
    emojiCount = 0;
    
    for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        if (c >= 1 && c <= EMOJI_MAP_SIZE/2) {
            width += EMOJI_SIZE + EMOJI_PADDING; // Add padding after emoji
            emojiCount++;
        } else {
            width += CHAR_WIDTH;
        }
    }
    
    // Remove trailing padding from last character
    if (emojiCount > 0) {
        width -= EMOJI_PADDING;
    }
}

bool isDST(time_t timestamp) {
    // Basic US DST calculation
    // Starts second Sunday in March, ends first Sunday in November
    struct tm* timeinfo = gmtime(&timestamp);
    int month = timeinfo->tm_mon + 1;
    int day = timeinfo->tm_mday;
    int wday = timeinfo->tm_wday;
    
    if (month < 3 || month > 11) {
        return false;
    }
    if (month > 3 && month < 11) {
        return true;
    }
    
    int weekNum = (day - 1) / 7 + 1;
    
    if (month == 3) {
        // Second Sunday or later
        return (weekNum >= 2 && wday == 0) || (weekNum > 2);
    } else {  // month == 11
        // Before first Sunday
        return (weekNum == 1 && wday < 0) || (weekNum < 1);
    }
}

void formatTimestamp(const String& timestampStr, char* formattedTime) {
    Serial.println("\nFormatting timestamp:");
    Serial.print("Input timestamp string: ");
    Serial.println(timestampStr);
    
    // Convert string to uint64_t instead of unsigned long
    uint64_t timestamp = strtoull(timestampStr.c_str(), NULL, 10);
    Serial.print("Converted to uint64_t: ");
    Serial.println((unsigned long)(timestamp)); // Print lower 32 bits
    Serial.println((unsigned long)(timestamp >> 32)); // Print upper 32 bits
    
    // Convert to seconds
    time_t timeStamp = timestamp / 1000;
    Serial.print("Converted to seconds: ");
    Serial.println(timeStamp);
    
    // Get UTC time
    struct tm* timeInfo = gmtime(&timeStamp);
    Serial.print("UTC Time - Hour: ");
    Serial.print(timeInfo->tm_hour);
    Serial.print(" Minute: ");
    Serial.print(timeInfo->tm_min);
    Serial.print(" Day: ");
    Serial.print(timeInfo->tm_mday);
    Serial.print(" Month: ");
    Serial.println(timeInfo->tm_mon + 1);
    
    // Apply EST offset
    timeStamp += (EST_OFFSET * SECONDS_PER_HOUR);
    Serial.print("After EST offset: ");
    Serial.println(timeStamp);
    
    // Convert to local time
    timeInfo = gmtime(&timeStamp);
    
    // Format final time
    int hour12 = timeInfo->tm_hour;
    if (hour12 == 0) {
        hour12 = 12;
    } else if (hour12 > 12) {
        hour12 -= 12;
    }
    
    sprintf(formattedTime, "%02d/%02d %02d:%02d%s", 
            timeInfo->tm_mon + 1,
            timeInfo->tm_mday,
            hour12,
            timeInfo->tm_min,
            timeInfo->tm_hour >= 12 ? "PM" : "AM");
            
    Serial.print("Final formatted time: ");
    Serial.println(formattedTime);
}

void drawTimestamp(const String& timestamp, int x, int y) {
    Serial.print("\nDrawing timestamp. Input: ");
    Serial.println(timestamp);
    
    char formattedTime[22];
    formatTimestamp(timestamp, formattedTime);
    
    Serial.print("Formatted timestamp to draw: ");
    Serial.println(formattedTime);
    
    int textWidth = strlen(formattedTime) * 6;
    int finalX = x - textWidth;
    
    Serial.print("Drawing at position x: ");
    Serial.print(finalX);
    Serial.print(" y: ");
    Serial.println(y);
    
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(finalX, y);
    tft.print(formattedTime);
}

void displayMailNotice() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    
    // Center "You've Got"
    const char* line1 = "You've Got";
    int16_t x1, y1;
    uint16_t w1, h1;
    tft.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
    tft.setCursor((tft.width() - w1) / 2, 20);
    tft.println(line1);
    
    // Center "Mail!"
    const char* line2 = "Mail!";
    int16_t x2, y2;
    uint16_t w2, h2;
    tft.getTextBounds(line2, 0, 0, &x2, &y2, &w2, &h2);
    tft.setCursor((tft.width() - w2) / 2, 45);
    tft.println(line2);
    
    // Center "Shake to read"
    tft.setTextSize(1);
    const char* line3 = "Shake to read";
    int16_t x3, y3;
    uint16_t w3, h3;
    tft.getTextBounds(line3, 0, 0, &x3, &y3, &w3, &h3);
    tft.setCursor((tft.width() - w3) / 2, 70);
    tft.println(line3);
    
    // Draw scaled emoji (2x size)
    for(int16_t j = 0; j < 16; j++) {
        for(int16_t i = 0; i < 16; i++) {
            uint16_t color = EMOJI_MAIL[j * 16 + i];
            if(color != 0) {  // Only draw non-transparent pixels
                // Draw a 2x2 square for each pixel
                int16_t x = (tft.width() - 32) / 2 + (i * 2);
                int16_t y = 90 + (j * 2);
                tft.fillRect(x, y, 2, 2, color);
            }
        }
    }
    
    lastActivityTime = millis();
}

void displayMessage(const String& message) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(1);
    
    // Process emoji codes
    String processedMessage = message;
    for (int i = 0; i < EMOJI_MAP_SIZE; i += 2) {
        String emojiText = EMOJI_MAP[i];
        int pos = processedMessage.indexOf(emojiText);
        while (pos >= 0) {
            processedMessage = processedMessage.substring(0, pos) + 
                             String((char)(1 + (i/2))) + 
                             processedMessage.substring(pos + emojiText.length());
            pos = processedMessage.indexOf(emojiText);
        }
    }
    
    // Split message into words
    String words[200];  // Adjust array size as needed
    int wordCount = 0;
    String currentWord = "";
    
    for (int i = 0; i < processedMessage.length(); i++) {
        char c = processedMessage.charAt(i);
        if (c == ' ' || c == '\n') {
            if (currentWord.length() > 0) {
                words[wordCount++] = currentWord;
                currentWord = "";
            }
            if (c == '\n') {
                words[wordCount++] = "\n";
            }
        } else {
            currentWord += c;
        }
    }
    if (currentWord.length() > 0) {
        words[wordCount++] = currentWord;
    }
    
    // Calculate lines needed
    String lines[100];  // Adjust array size as needed
    int lineCount = 0;
    String currentLine = "";
    
    for (int i = 0; i < wordCount; i++) {
        if (words[i] == "\n") {
            if (currentLine.length() > 0) {
                lines[lineCount++] = currentLine;
                currentLine = "";
            }
            continue;
        }
        
        // Calculate width of current line plus new word
        String testLine = currentLine;
        if (testLine.length() > 0) testLine += " ";
        testLine += words[i];
        
        // Calculate actual width including emojis and padding
        int testWidth = 0;
        int testEmojiCount = 0;
        calculateTextDimensions(testLine, testWidth, testEmojiCount);
        
        if (testWidth <= (EFFECTIVE_WIDTH - (2 * BUBBLE_PADDING))) {
            currentLine = testLine;
        } else {
            if (currentLine.length() > 0) {
                lines[lineCount++] = currentLine;
            }
            currentLine = words[i];
        }
    }
    if (currentLine.length() > 0) {
        lines[lineCount++] = currentLine;
    }
    
    // Calculate vertical centering
    int totalTextHeight = lineCount * LINE_HEIGHT;
    int startY = (SCREEN_HEIGHT - totalTextHeight) / 2;
    
    // Find maximum line width for bubble sizing
    int maxLineWidth = 0;
    for (int i = 0; i < lineCount; i++) {
        int lineWidth = 0;
        int emojiCount = 0;
        calculateTextDimensions(lines[i], lineWidth, emojiCount);
        maxLineWidth = max(maxLineWidth, lineWidth);
    }
    
    // Calculate bubble dimensions
    int bubbleWidth = maxLineWidth + (BUBBLE_PADDING * 2);
    int bubbleHeight = totalTextHeight + (BUBBLE_PADDING * 2);
    int bubbleX = MARGIN + ((EFFECTIVE_WIDTH - bubbleWidth) / 2);
    int bubbleY = startY - BUBBLE_PADDING;
    
    // Draw message bubble
    tft.fillRoundRect(bubbleX, bubbleY, bubbleWidth, bubbleHeight, CORNER_RADIUS, 0x630C);
    
    // Display text
    if (lineCount > LINES_PER_SCREEN) {
        isScrolling = true;
        currentMessage = processedMessage;
        scrollMessage();
    } else {
        isScrolling = false;
        for (int i = 0; i < lineCount; i++) {
            String line = lines[i];
            int lineWidth = 0;
            int emojiCount = 0;
            calculateTextDimensions(line, lineWidth, emojiCount);
            
            // Center each line horizontally
            int startX = bubbleX + BUBBLE_PADDING + ((bubbleWidth - (2 * BUBBLE_PADDING) - lineWidth) / 2);
            int currentX = startX;
            int currentY = startY + (i * LINE_HEIGHT);
            
            for (int j = 0; j < line.length(); j++) {
                char c = line.charAt(j);
                
                if (c >= 1 && c <= EMOJI_MAP_SIZE/2) {
                    const uint16_t* emoji = getEmojiFromText(EMOJI_MAP[(c-1)*2]);
                    if (emoji != nullptr) {
                        // Center the emoji vertically within the line
                        int emojiY = currentY + ((LINE_HEIGHT - EMOJI_SIZE) / 2);
                        drawEmoji(&tft, emoji, currentX, emojiY);
                        currentX += EMOJI_SIZE + EMOJI_PADDING;
                    }
                } else {
                    // Center the text vertically within the line
                    int textY = currentY + ((LINE_HEIGHT - 8) / 2);  // 8 is text height
                    tft.setCursor(currentX, textY);
                    tft.print(c);
                    currentX += CHAR_WIDTH;
                }
            }
        }
    }
    
    if (!isScrolling) {
        // Display timestamp further from right edge to accommodate longer format
        drawTimestamp(currentTimestamp, 
                     SCREEN_WIDTH - 5,  // 5 pixels from right edge
                     SCREEN_HEIGHT - 10); // 10 pixels from bottom
    }
    
    lastActivityTime = millis();
}

void resetScrollState() {
    scrollState.currentChunk = 0;
    scrollState.totalChunks = 0;
    scrollState.lastScrollTime = 0;
    scrollState.hasCompletedOnePass = false;
    scrollState.isInitialized = false;
    for(int i = 0; i < 100; i++) {
        scrollState.displayChunks[i] = "";
    }
}

void scrollMessage() {
    // If not initialized, process the message into chunks
    if (!scrollState.isInitialized) {
        // Process escape sequences
        String processedMessage = "";
        for (int i = 0; i < currentMessage.length(); i++) {
            if (currentMessage.charAt(i) == '\\' && i + 1 < currentMessage.length()) {
                switch (currentMessage.charAt(i + 1)) {
                    case 'n':
                        processedMessage += '\n';
                        i++;
                        break;
                    case '\\':
                        processedMessage += '\\';
                        i++;
                        break;
                    default:
                        processedMessage += currentMessage.charAt(i);
                }
            } else {
                processedMessage += currentMessage.charAt(i);
            }
        }
        
        // Split into words and build chunks
        String currentChunkText = "";
        String currentLine = "";
        int currentLineCount = 0;
        
        // Split message into words
        String tempMessage = processedMessage;
        while (tempMessage.length() > 0) {
            String word;
            int spaceIndex = tempMessage.indexOf(' ');
            int newlineIndex = tempMessage.indexOf('\n');
            
            if (spaceIndex == -1 && newlineIndex == -1) {
                word = tempMessage;
                tempMessage = "";
            } else {
                int nextBreak = (spaceIndex == -1) ? newlineIndex : 
                               (newlineIndex == -1) ? spaceIndex :
                               min(spaceIndex, newlineIndex);
                
                word = tempMessage.substring(0, nextBreak);
                tempMessage = tempMessage.substring(nextBreak + 1);
                
                if (nextBreak == newlineIndex) {
                    word += "\n";
                }
            }
            
            // Handle the word
            if (word.endsWith("\n")) {
                word = word.substring(0, word.length() - 1);
                // If word fits on current line, add it
                if (currentLine.length() + (currentLine.isEmpty() ? 0 : 1) + word.length() <= CHARS_PER_LINE) {
                    if (!currentLine.isEmpty()) currentLine += " ";
                    currentLine += word;
                } else if (!word.isEmpty()) {
                    // Word doesn't fit, start new line
                    if (!currentLine.isEmpty()) {
                        if (!currentChunkText.isEmpty()) currentChunkText += "\n";
                        currentChunkText += currentLine;
                        currentLineCount++;
                    }
                    currentLine = word;
                }
                
                // Add the current line to chunk
                if (!currentLine.isEmpty()) {
                    if (!currentChunkText.isEmpty()) currentChunkText += "\n";
                    currentChunkText += currentLine;
                    currentLineCount++;
                }
                currentLine = "";
                
                // Force a new line
                currentLineCount++;
                if (currentLineCount >= MAX_LINES) {
                    scrollState.displayChunks[scrollState.totalChunks++] = currentChunkText;
                    currentChunkText = "";
                    currentLineCount = 0;
                } else {
                    currentChunkText += "\n";
                }
            } else {
                // Handle regular words
                if (currentLine.length() + (currentLine.isEmpty() ? 0 : 1) + word.length() <= CHARS_PER_LINE) {
                    if (!currentLine.isEmpty()) currentLine += " ";
                    currentLine += word;
                } else {
                    if (!currentLine.isEmpty()) {
                        if (!currentChunkText.isEmpty()) currentChunkText += "\n";
                        currentChunkText += currentLine;
                        currentLineCount++;
                        
                        if (currentLineCount >= MAX_LINES) {
                            scrollState.displayChunks[scrollState.totalChunks++] = currentChunkText;
                            currentChunkText = "";
                            currentLineCount = 0;
                        }
                    }
                    currentLine = word;
                }
            }
        }
        
        // Handle remaining text
        if (!currentLine.isEmpty()) {
            if (!currentChunkText.isEmpty()) currentChunkText += "\n";
            currentChunkText += currentLine;
            currentLineCount++;
        }
        if (!currentChunkText.isEmpty()) {
            scrollState.displayChunks[scrollState.totalChunks++] = currentChunkText;
        }
        
        // Safety check
        if (scrollState.totalChunks == 0) {
            scrollState.totalChunks = 1;
        }
        
        scrollState.isInitialized = true;
    }
    
    // Handle display timing
    if (millis() - scrollState.lastScrollTime >= SCROLL_DELAY) {
        tft.fillScreen(ST77XX_BLACK);
        
        // Get current chunk text
        String currentText = scrollState.displayChunks[scrollState.currentChunk];
        
        // Split into lines and calculate dimensions first
        String lines[MAX_LINES] = {""};
        int lineCount = 0;
        
        // Split chunk into lines
        int startPos = 0;
        int newlinePos = currentText.indexOf('\n');
        while (newlinePos != -1 && lineCount < MAX_LINES) {
            lines[lineCount++] = currentText.substring(startPos, newlinePos);
            startPos = newlinePos + 1;
            newlinePos = currentText.indexOf('\n', startPos);
        }
        if (startPos < currentText.length() && lineCount < MAX_LINES) {
            lines[lineCount++] = currentText.substring(startPos);
        }
        
        // Calculate maximum width including emojis
        int maxLineWidth = 0;
        for (int i = 0; i < lineCount; i++) {
            int lineWidth = 0;
            int emojiCount = 0;
            calculateTextDimensions(lines[i], lineWidth, emojiCount);
            maxLineWidth = max(maxLineWidth, lineWidth);
        }
        
        // Calculate bubble dimensions with accurate widths
        int bubbleWidth = maxLineWidth + (BUBBLE_PADDING * 2);
        int bubbleHeight = (lineCount * LINE_HEIGHT) + (BUBBLE_PADDING * 2);
        if (scrollState.totalChunks > 1 && scrollState.currentChunk < scrollState.totalChunks - 1) {
            bubbleHeight += ELLIPSIS_HEIGHT;
        }
        int bubbleX = MARGIN + ((EFFECTIVE_WIDTH - bubbleWidth) / 2);
        int bubbleY = BUBBLE_PADDING;
        
        // Draw message bubble
        tft.fillRoundRect(bubbleX, bubbleY, bubbleWidth, bubbleHeight, CORNER_RADIUS, 0x630C);
        
        // Display text
        for (int i = 0; i < lineCount; i++) {
            String line = lines[i];
            int lineWidth = 0;
            int emojiCount = 0;
            calculateTextDimensions(line, lineWidth, emojiCount);
            
            int startX = bubbleX + BUBBLE_PADDING + ((bubbleWidth - (2 * BUBBLE_PADDING) - lineWidth) / 2);
            int currentX = startX;
            int baseY = bubbleY + BUBBLE_PADDING + (i * LINE_HEIGHT);
            
            for (int j = 0; j < line.length(); j++) {
                char c = line.charAt(j);
                
                if (c >= 1 && c <= EMOJI_MAP_SIZE/2) {
                    const uint16_t* emoji = getEmojiFromText(EMOJI_MAP[(c-1)*2]);
                    if (emoji != nullptr) {
                        int emojiY = baseY + ((LINE_HEIGHT - EMOJI_SIZE) / 2);
                        drawEmoji(&tft, emoji, currentX, emojiY);
                        currentX += EMOJI_SIZE + EMOJI_PADDING;
                    }
                } else {
                    int textY = baseY + ((LINE_HEIGHT - 8) / 2);
                    tft.setCursor(currentX, textY);
                    tft.print(c);
                    currentX += CHAR_WIDTH;
                }
            }
        }
        
        // Draw ellipsis if not the last chunk
        if (scrollState.totalChunks > 1 && scrollState.currentChunk < scrollState.totalChunks - 1) {
            const int DOT_SIZE = 3;
            const int DOT_SPACING = 6;
            const int TOTAL_DOT_WIDTH = (DOT_SIZE * 3) + (DOT_SPACING * 2);
            int dotX = bubbleX + (bubbleWidth - TOTAL_DOT_WIDTH) / 2;
            int dotY = bubbleY + bubbleHeight - BUBBLE_PADDING - DOT_SIZE;
            
            for (int i = 0; i < 3; i++) {
                tft.fillCircle(dotX + (i * (DOT_SIZE + DOT_SPACING)), dotY, DOT_SIZE/2, ST77XX_WHITE);
            }
        }
        
        // Draw timestamp in bottom right
        drawTimestamp(currentTimestamp, 
                     SCREEN_WIDTH - 5,
                     SCREEN_HEIGHT - 10);

        // Draw fraction in bottom left corner
        if (scrollState.totalChunks > 1) {
            tft.setTextColor(ST77XX_WHITE);
            tft.setTextSize(1);
            String fraction = String(scrollState.currentChunk + 1) + "/" + String(scrollState.totalChunks);
            tft.setCursor(5, SCREEN_HEIGHT - 10);
            tft.print(fraction);
        }
        
        scrollState.lastScrollTime = millis();
        
        // Move to next chunk or loop back to start
        scrollState.currentChunk++;
        if (scrollState.currentChunk >= scrollState.totalChunks) {
            scrollState.currentChunk = 0;
            scrollState.hasCompletedOnePass = true;
            delay(SCROLL_DELAY * 2);
        }
        
        // Only update lastActivityTime if we haven't completed one pass
        if (!scrollState.hasCompletedOnePass) {
            lastActivityTime = millis();
        }
    }
}

// State Machine Helper Functions
bool checkShakeDetection() {
    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;
    imu.getEvent(&accel, &gyro, &temp);
    
    float acceleration = sqrt(
        accel.acceleration.x * accel.acceleration.x +
        accel.acceleration.y * accel.acceleration.y +
        accel.acceleration.z * accel.acceleration.z
    );
    
    return acceleration > SHAKE_THRESHOLD;
}

void checkShake() {
   if (millis() - lastShakeTime >= SHAKE_CHECK_INTERVAL) {
       if (checkShakeDetection()) {
           switch (currentState) {
               case SLEEP:
                   if (isScrolling) {
                       // Don't reset scroll state, just wake the display
                       wakeDisplay();
                   } else {
                       isFirstEntryOpen = true;
                   }
                   currentState = OPEN;
                   break;
                   
               case UNOPEN:
                   
                   tone(BUZZER_PIN, NOTE_FS5, 100);
                   tone(BUZZER_PIN, NOTE_A5, 175);

                   isFirstEntryOpen = true;
                   resetScrollState();  // Reset scroll state for new message
                   currentState = OPEN;
                   break;
                   
               default:
                   break;
           }
           lastActivityTime = millis();
       }
       lastShakeTime = millis();
   }
}

bool checkForNewMessages() {
   if (millis() - lastDBCheck >= DB_CHECK_INTERVAL) {
       bool hasNew = Database.get<bool>(aClient, "messages/has_new");
       if (aClient.lastError().code() != 0) {
           handleError("Failed to check for new messages");
           return false;
       }
       
       if (hasNew) {
           String messageId = Database.get<String>(aClient, "messages/current/id");
           String messageContent = Database.get<String>(aClient, "messages/current/content");
           
           Serial.print("Message ID: ");
           Serial.println(messageId);
           
           // Construct the timestamp path
           String timestampPath = "messages/history/" + messageId + "/timestamp";
           Serial.print("Timestamp path: ");
           Serial.println(timestampPath);
           
           String timestamp = Database.get<String>(aClient, timestampPath.c_str());
           Serial.print("Raw timestamp from Firebase: ");
           Serial.println(timestamp);
           
           if (aClient.lastError().code() == 0) {
               messageId.replace("\\\"", "");
               messageId.replace("\"", "");
               messageContent.replace("\\\"", "");
               messageContent.replace("\"", "");
               timestamp.replace("\\\"", "");
               timestamp.replace("\"", "");
               
               if (messageId.length() > 0) {
                   if (messageContent.length() > MAX_MESSAGE_LENGTH) {
                       messageContent = messageContent.substring(0, MAX_MESSAGE_LENGTH);
                   }
                   
                   Serial.println("New message received:");
                   Serial.println(messageContent);
                   
                   currentMessage = messageContent;
                   lastMessageId = messageId;
                   currentTimestamp = timestamp;
                   hasUnreadMessage = true;

                   // Save to preferences
                   preferences.putString("lastMsgId", lastMessageId);
                   preferences.putString("lastMsg", currentMessage);
                   preferences.putString("lastTimestamp", currentTimestamp);

                   lastDBCheck = millis();
                   return true;
               }
           }
       }
       lastDBCheck = millis();
   }
   return false;
}

void updateMessageStatus() {
    // First update the current message status
    bool status = Database.set<bool>(aClient, "messages/has_new", false);
    if (!status) {
        handleError("Failed to update new message status");
        return;
    }

    // Update read status in current message
    status = Database.set<bool>(aClient, "messages/current/read", true);
    if (!status) {
        handleError("Failed to update read status");
        return;
    }

    // Update history with the read status
    if (lastMessageId.length() > 0) {
        String historyPath = String("messages/history/") + lastMessageId;
        
        // Update read status
        status = Database.set<bool>(aClient, (historyPath + "/read").c_str(), true);
        if (!status) {
            handleError("Failed to update history read status");
            return;
        }

        // Update timestamp (optional)
        unsigned long timestamp = millis();
        status = Database.set<unsigned long>(aClient, (historyPath + "/read_timestamp").c_str(), timestamp);
        if (!status) {
            handleError("Failed to update history timestamp");
            return;
        }
    }

    // Clear local state and force immediate check for new messages
    hasUnreadMessage = false;
    lastMessageId = "";
    lastDBCheck = 0;  // Add this line to force immediate check
}

void setup() {
    Serial.begin(115200);

    // Open preferences with namespace "msgbox"
    preferences.begin("msgbox", false);

    // Retrieve last message ID and content
    lastMessageId = preferences.getString("lastMsgId", "");
    currentMessage = preferences.getString("lastMsg", "");
    currentTimestamp = preferences.getString("lastTimestamp", "");

    // Debug print
    Serial.println("Loaded from preferences:");
    Serial.println("Message ID: " + lastMessageId);
    Serial.println("Timestamp: " + currentTimestamp);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(TFT_BL, OUTPUT);

    //analogWrite(TFT_BL, 255); // Start with full brightness

    tone(BUZZER_PIN, NOTE_FS5, 100);
    tone(BUZZER_PIN, NOTE_A5, 175);
    
    setupDisplay();
    setupIMU();
    setupWiFi();
    setupFirebase();
    //setupSMTP();
    
    currentState = OPEN;
    lastActivityTime = millis();
    lastDBCheck = millis();
    lastShakeTime = millis();
}

void loop() {
   // Process database operations
   Database.loop();
   
   // Check for new messages in all states except when already unread message exists
   if (!hasUnreadMessage && currentState != INITIALIZING) {
       if (checkForNewMessages()) {
           isFirstEntryUnopen = true;
           currentState = UNOPEN;
           if (currentState == SLEEP) {
               wakeDisplay();
           }
       }
   }
   
   // Main state machine
   switch (currentState) {
       // In the OPEN case of your state machine:
case OPEN:
    if (isFirstEntryOpen) {
        displayMessage(currentMessage);
        updateMessageStatus();
        isFirstEntryOpen = false;
        lastActivityTime = millis();  // Reset activity timer when first showing message
    }
    
    if (isScrolling) {
        scrollMessage();
        // Only go to sleep after scrolling completes AND timeout occurs
        if (scrollState.hasCompletedOnePass && millis() - lastActivityTime >= SLEEP_TIMEOUT) {
            isFirstEntrySleep = true;
            currentState = SLEEP;
        }
    } else {
        // For non-scrolling messages, only go to sleep after timeout
        if (millis() - lastActivityTime >= SLEEP_TIMEOUT) {
            isFirstEntrySleep = true;
            currentState = SLEEP;
        }
    }
    
    checkShake();
    break;
           
       case SLEEP:
           if (isFirstEntrySleep) {
               fadeDisplay();
               isFirstEntrySleep = false;
           }
           
           checkShake();
           break;
           
       case UNOPEN:
           if (isFirstEntryUnopen) {
               displayMailNotice();
               
              tone(BUZZER_PIN, NOTE_FS5, 100);
              tone(BUZZER_PIN, NOTE_A5, 175);

               isFirstEntryUnopen = false;
           }
           
           checkShake();
           break;
   }
}