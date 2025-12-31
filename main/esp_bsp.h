/*
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: MIT
 *
 * Board Support Package for ESP32-32E 4.0" Display (E32R40T)
 * - ST7796S LCD (320x480) via SPI
 * - XPT2046 Resistive Touch via SPI (shared bus)
 * - Based on lcdwiki.com/4.0inch_ESP32-32E_Display
 */

#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "lv_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== Display Configuration ========================== */

// LCD Resolution (landscape mode with hardware rotation: 480x320)
#define BSP_LCD_H_RES               480
#define BSP_LCD_V_RES               320

// LCD SPI Configuration
#define BSP_LCD_SPI_HOST            SPI2_HOST
#define BSP_LCD_SPI_CLK_FREQ        (40 * 1000 * 1000)  // 40 MHz for ST7796 (Freenove uses 80MHz, 40 is safer)

// LCD Pin Definitions (from lcdwiki ESP32-32E / E32R40T)
#define BSP_LCD_PIN_CS              GPIO_NUM_15   // LCD chip select, active low
#define BSP_LCD_PIN_DC              GPIO_NUM_2    // Command/Data: High=Data, Low=Command
#define BSP_LCD_PIN_CLK             GPIO_NUM_14   // SPI clock (shared with touch)
#define BSP_LCD_PIN_MOSI            GPIO_NUM_13   // SPI MOSI (shared with touch)
#define BSP_LCD_PIN_MISO            GPIO_NUM_12   // SPI MISO (shared with touch)
#define BSP_LCD_PIN_RST             GPIO_NUM_NC   // LCD reset shares EN with ESP32
#define BSP_LCD_PIN_BL              GPIO_NUM_27   // Backlight control, high = on

// LVGL Buffer Configuration (reduced for 4MB flash / no PSRAM)
#define BSP_LCD_DRAW_BUF_HEIGHT     20            // Buffer height in lines

/* ========================== Touch Configuration ========================== */

// Touch SPI Configuration (shares SPI bus with LCD)
#define BSP_TOUCH_SPI_HOST          SPI2_HOST     // Same as LCD
#define BSP_TOUCH_SPI_CLK_FREQ      (1 * 1000 * 1000)  // 1 MHz for XPT2046

// Touch Pin Definitions
#define BSP_TOUCH_PIN_CS            GPIO_NUM_33   // Touch chip select, active low
#define BSP_TOUCH_PIN_IRQ           GPIO_NUM_36   // Touch interrupt, low when touched

/* ========================== RGB LED Configuration ========================== */

// RGB LED (active low - common anode)
#define BSP_LED_RED                 GPIO_NUM_22
#define BSP_LED_GREEN               GPIO_NUM_16
#define BSP_LED_BLUE                GPIO_NUM_17

/* ========================== Audio Configuration ========================== */

#define BSP_AUDIO_ENABLE            GPIO_NUM_4    // Enable at low level
#define BSP_AUDIO_DAC               GPIO_NUM_26   // DAC output

/* ========================== Battery Configuration ========================== */

#define BSP_BATTERY_ADC             GPIO_NUM_34   // ADC input for battery voltage

/* ========================== Buttons Configuration ========================== */

#define BSP_BUTTON_BOOT             GPIO_NUM_0    // Boot/Download button

/* ========================== Legacy Compatibility Defines ========================== */

// For compatibility with existing code (guard against redefinition from display.h)
#ifndef EXAMPLE_LCD_QSPI_H_RES
#define EXAMPLE_LCD_QSPI_H_RES      BSP_LCD_H_RES
#endif
#ifndef EXAMPLE_LCD_QSPI_V_RES
#define EXAMPLE_LCD_QSPI_V_RES      BSP_LCD_V_RES
#endif

/* ========================== BSP Display Configuration Structure ========================== */

/**
 * @brief BSP display configuration structure
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< Configuration for the LVGL port */
    uint32_t buffer_size;           /*!< Size of the buffer for the screen in pixels */
    lv_disp_rot_t rotate;           /*!< Rotation configuration for the display */
} bsp_display_cfg_t;

/* ========================== Function Declarations ========================== */

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

/**
 * @brief Get pointer to input device (touch, buttons, ...)
 *
 * @note The LVGL input device is initialized in bsp_display_start() function.
 *
 * @return Pointer to LVGL input device or NULL when not initialized
 */
lv_indev_t *bsp_display_get_input_dev(void);

/**
 * @brief Set backlight brightness
 *
 * @param brightness_percent Brightness percentage (0-100)
 * @return ESP_OK on success
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);

/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return true  Mutex was taken
 * @return false Mutex was NOT taken
 */
bool bsp_display_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 */
void bsp_display_unlock(void);

/**
 * @brief Set RGB LED color
 *
 * @param red Red LED on (true = LED on)
 * @param green Green LED on
 * @param blue Blue LED on
 */
void bsp_led_set(bool red, bool green, bool blue);

#ifdef __cplusplus
}
#endif
