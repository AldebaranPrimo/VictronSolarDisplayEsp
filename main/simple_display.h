/**
 * Simple ST7796 Display Driver - No LVGL
 * Direct SPI communication for Freenove ESP32 Display
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

// Colors RGB565
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_YELLOW  0xFFE0
#define COLOR_ORANGE  0xFD20

// Display dimensions (native portrait)
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 480

/**
 * @brief Initialize the display
 */
esp_err_t display_init(void);

/**
 * @brief Fill entire screen with a color
 */
void display_fill(uint16_t color);

/**
 * @brief Fill a rectangle
 */
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a single pixel
 */
void display_pixel(int x, int y, uint16_t color);

/**
 * @brief Draw a character (8x16 font)
 */
void display_char(int x, int y, char c, uint16_t fg, uint16_t bg);

/**
 * @brief Draw a string
 */
void display_string(int x, int y, const char *str, uint16_t fg, uint16_t bg);

/**
 * @brief Draw a large string (16x32 font, 2x scale)
 */
void display_string_large(int x, int y, const char *str, uint16_t fg, uint16_t bg);

/**
 * @brief Set backlight brightness (0-100)
 */
void display_set_brightness(int percent);
