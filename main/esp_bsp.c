/*
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: MIT
 *
 * Board Support Package for ESP32-32E 4.0" Display (E32R40T)
 * ST7796S LCD + XPT2046 Resistive Touch
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"

#include "lv_port.h"
#include "esp_bsp.h"
#include "bsp_err_check.h"

// ST7796 LCD panel driver
#include "esp_lcd_st7796.h"

// XPT2046 touch controller (via SPI) - atanisoft component
#include "esp_lcd_touch_xpt2046.h"

static const char *TAG = "BSP_E32R40T";

/* ========================== LCD Configuration ========================== */

// LCD bits per pixel
#define BSP_LCD_BITS_PER_PIXEL      16

// LEDC channel for backlight PWM
#define LCD_LEDC_CH                 LEDC_CHANNEL_0

/* ========================== Static Variables ========================== */

static lv_disp_t *disp = NULL;
static lv_indev_t *disp_indev = NULL;
static esp_lcd_touch_handle_t tp = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static bool spi_bus_initialized = false;

/* ========================== Backlight Functions ========================== */

static esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_timer_config_t backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&backlight_timer));

    const ledc_channel_config_t backlight_channel = {
        .gpio_num = BSP_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    BSP_ERROR_CHECK_RETURN_ERR(ledc_channel_config(&backlight_channel));

    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100;
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));

    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

/* ========================== RGB LED Functions ========================== */

void bsp_led_set(bool red, bool green, bool blue)
{
    // LEDs are common anode (active low)
    gpio_set_level(BSP_LED_RED, !red);
    gpio_set_level(BSP_LED_GREEN, !green);
    gpio_set_level(BSP_LED_BLUE, !blue);
}

static esp_err_t bsp_led_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BSP_LED_RED) | (1ULL << BSP_LED_GREEN) | (1ULL << BSP_LED_BLUE),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Turn off all LEDs initially
    bsp_led_set(false, false, false);
    return ESP_OK;
}

/* ========================== SPI Bus Init ========================== */

static esp_err_t bsp_spi_bus_init(void)
{
    if (spi_bus_initialized) {
        return ESP_OK; // Already initialized
    }

    ESP_LOGI(TAG, "Initialize SPI bus for LCD and Touch");
    
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LCD_PIN_MOSI,
        .miso_io_num = BSP_LCD_PIN_MISO,
        .sclk_io_num = BSP_LCD_PIN_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_DRAW_BUF_HEIGHT * sizeof(uint16_t),
    };
    
    BSP_ERROR_CHECK_RETURN_ERR(spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    spi_bus_initialized = true;
    
    return ESP_OK;
}

/* ========================== LCD Display Init ========================== */

static esp_err_t bsp_display_new(esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;

    // Initialize SPI bus first
    BSP_ERROR_CHECK_RETURN_ERR(bsp_spi_bus_init());

    ESP_LOGI(TAG, "Install panel IO for ST7796");
    
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_PIN_DC,
        .cs_gpio_num = BSP_LCD_PIN_CS,
        .pclk_hz = BSP_LCD_SPI_CLK_FREQ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_config, ret_io),
        err, TAG, "Failed to create panel IO"
    );

    ESP_LOGI(TAG, "Install ST7796 LCD driver");
    
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,  // ST7796 uses BGR natively
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
    };
    
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_st7796(*ret_io, &panel_config, ret_panel),
        err, TAG, "Failed to create ST7796 panel"
    );

    // Reset and initialize the display
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(*ret_panel), err, TAG, "Panel reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for display to reset
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(*ret_panel), err, TAG, "Panel init failed");
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for display to initialize;
    
    // Invert colors if needed (ST7796 specific)
    ESP_GOTO_ON_ERROR(esp_lcd_panel_invert_color(*ret_panel, false), err, TAG, "Invert color failed");
    
    // Hardware rotation: landscape mode (480x320)
    // swap_xy=true rotates 90 degrees, mirror adjusts orientation
    ESP_GOTO_ON_ERROR(esp_lcd_panel_swap_xy(*ret_panel, true), err, TAG, "Swap XY failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_mirror(*ret_panel, true, false), err, TAG, "Mirror failed");
    
    // Turn on display
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), err, TAG, "Display on failed");

    return ESP_OK;

err:
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
        *ret_panel = NULL;
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
        *ret_io = NULL;
    }
    return ret;
}

/* ========================== Touch Init ========================== */

static esp_err_t bsp_touch_new(const bsp_display_cfg_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = ESP_OK;

    // Make sure SPI bus is initialized
    BSP_ERROR_CHECK_RETURN_ERR(bsp_spi_bus_init());

    ESP_LOGI(TAG, "Initialize XPT2046 touch controller");

    // Create SPI IO handle for touch using atanisoft's macro
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(BSP_TOUCH_PIN_CS);
    
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &tp_io_config, &tp_io_handle),
        err, TAG, "Failed to create touch IO"
    );

    // Configure touch panel
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BSP_TOUCH_PIN_IRQ,
        .levels = {
            .reset = 0,
            .interrupt = 0,  // Active low
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    // Handle rotation
    if (config->rotate == LV_DISP_ROT_90 || config->rotate == LV_DISP_ROT_270) {
        tp_cfg.x_max = BSP_LCD_V_RES;
        tp_cfg.y_max = BSP_LCD_H_RES;
        tp_cfg.flags.swap_xy = 1;
    }
    if (config->rotate == LV_DISP_ROT_180 || config->rotate == LV_DISP_ROT_270) {
        tp_cfg.flags.mirror_x = 1;
        tp_cfg.flags.mirror_y = 1;
    }

    ESP_GOTO_ON_ERROR(
        esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, ret_touch),
        err, TAG, "Failed to create XPT2046 touch"
    );

    return ESP_OK;

err:
    if (tp_io_handle) {
        esp_lcd_panel_io_del(tp_io_handle);
    }
    return ret;
}

/* ========================== LVGL Display Init ========================== */

static lv_disp_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;

    // Create display
    if (bsp_display_new(&panel_handle, &io_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return NULL;
    }

    // Determine resolution based on rotation
    uint32_t hres = BSP_LCD_H_RES;
    uint32_t vres = BSP_LCD_V_RES;
    
    if (cfg->rotate == LV_DISP_ROT_90 || cfg->rotate == LV_DISP_ROT_270) {
        hres = BSP_LCD_V_RES;
        vres = BSP_LCD_H_RES;
    }

    ESP_LOGI(TAG, "Add LCD screen to LVGL (hres=%lu, vres=%lu)", hres, vres);
    
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = cfg->buffer_size,
        .sw_rotate = cfg->rotate,
        .hres = hres,
        .vres = vres,
        .trans_size = 0,  // Disable transport buffer to save memory
        .draw_wait_cb = NULL,  // No tear sync needed for this display
        .flags = {
            .buff_dma = true,     // Use DMA-capable memory
            .buff_spiram = false, // ESP32-32E has no PSRAM
        },
    };

    return lvgl_port_add_disp(&disp_cfg);
}

/* ========================== LVGL Touch Init ========================== */

static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *config, lv_disp_t *disp)
{
    if (bsp_touch_new(config, &tp) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch");
        return NULL;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
        .touch_wait_cb = NULL,
    };

    return lvgl_port_add_touch(&touch_cfg);
}

/* ========================== Public API ========================== */

lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    ESP_LOGI(TAG, "Starting display for ESP32-32E (E32R40T)");

    // Initialize LEDs
    bsp_led_init();
    
    // Indicate startup with blue LED
    bsp_led_set(false, false, true);

    // Initialize LVGL port
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));

    // Initialize backlight
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());

    // Initialize LCD display
    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    // Initialize touch input
    BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(cfg, disp), NULL);

    // Turn on backlight
    bsp_display_brightness_set(100);
    
    // Indicate success with green LED
    bsp_led_set(false, true, false);

    return disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
