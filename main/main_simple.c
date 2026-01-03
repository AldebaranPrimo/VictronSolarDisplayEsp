/**
 * Victron Solar Display - Simple Version
 * No LVGL, direct display driver, hardcoded AES keys
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "simple_display.h"
#include "victron_ble.h"
#include "victron_records.h"
#include "ui_bars.h"

static const char *TAG = "VICTRON";

// Stub for ui_set_ble_mac (required by victron_ble.c)
void ui_set_ble_mac(const uint8_t *mac) {
    (void)mac;
}

// Current data storage
static SemaphoreHandle_t data_mutex = NULL;
static victron_data_t current_solar = {0};
static victron_data_t current_battery = {0};
static victron_data_t current_smartshunt = {0};
static bool has_solar_data = false;
static bool has_battery_data = false;
static bool has_smartshunt_data = false;

// Helper to get state string
static const char* get_state_string(uint8_t state) {
    switch (state) {
        case VIC_STATE_OFF: return "OFF";
        case VIC_STATE_LOW_POWER: return "LOW PWR";
        case VIC_STATE_FAULT: return "FAULT";
        case VIC_STATE_BULK: return "BULK";
        case VIC_STATE_ABSORPTION: return "ABSORB";
        case VIC_STATE_FLOAT: return "FLOAT";
        case VIC_STATE_STORAGE: return "STORAGE";
        case VIC_STATE_EQUALIZE: return "EQUAL";
        case VIC_STATE_POWER_SUPPLY: return "PSU";
        default: return "---";
    }
}

// Victron data callback
static void victron_data_callback(const victron_data_t *data) {
    if (!data || !data_mutex) return;
    
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    
    if (data->device_id == VICTRON_DEVICE_MPPT) {
        memcpy(&current_solar, data, sizeof(victron_data_t));
        has_solar_data = true;
        ESP_LOGI(TAG, "MPPT: %.2fV %.1fA %dW", 
            data->record.solar.battery_voltage_centi / 100.0f,
            data->record.solar.battery_current_deci / 10.0f,
            data->record.solar.pv_power_w);
    }
    else if (data->device_id == VICTRON_DEVICE_SMARTSHUNT) {
        memcpy(&current_smartshunt, data, sizeof(victron_data_t));
        has_smartshunt_data = true;
        ESP_LOGI(TAG, "SmartShunt: %.2fV %.1f%% %.2fA", 
            data->record.battery.battery_voltage_centi / 100.0f,
            data->record.battery.soc_deci_percent / 10.0f,
            data->record.battery.battery_current_milli / 1000.0f);
    }
    else if (data->device_id == VICTRON_DEVICE_BATTERY_SENSE) {
        // SmartBatterySense - only voltage and temperature
        memcpy(&current_battery, data, sizeof(victron_data_t));
        has_battery_data = true;
        // Temperature is in aux_value when aux_input == 2 (Kelvin * 100)
        float temp_c = (data->record.battery.aux_value / 100.0f) - 273.15f;
        ESP_LOGI(TAG, "BatterySense: %.2fV %.1f°C (aux_mode=%d)", 
            data->record.battery.battery_voltage_centi / 100.0f,
            temp_c,
            data->record.battery.aux_input);
    }
    
    xSemaphoreGive(data_mutex);
}

// Draw the main UI - 3 sections layout (no header/footer, maximized space)
static void draw_ui(void) {
    char buf[64];

    // Landscape layout: 4 quadranti (2x2)
    const int half_w = DISPLAY_WIDTH / 2;   // 240
    const int half_h = DISPLAY_HEIGHT / 2;  // 160
    const int pad = 8;
    const int inner_w = half_w - pad * 2;
    const int bar_w = inner_w - 4;

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    
    // === Q1: MPPT SOLAR CHARGER (top-left) ===
    int base_x = 0;
    int base_y = 0;
    display_fill_rect(base_x, base_y, half_w, half_h, COLOR_BLACK);
    display_string(base_x + pad, base_y + pad, "MPPT SOLAR CHARGER", COLOR_YELLOW, COLOR_BLACK);
    display_string(base_x + half_w - pad - 24, base_y + pad, has_solar_data ? "    " : "(--)", has_solar_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);

    int y = base_y + pad + 18;
    {
        int pv_power = has_solar_data ? current_solar.record.solar.pv_power_w : 0;
        const char *state = has_solar_data ? get_state_string(current_solar.record.solar.device_state) : "OFF";
        float voltage = has_solar_data ? current_solar.record.solar.battery_voltage_centi / 100.0f : 0.0f;
        float current = has_solar_data ? current_solar.record.solar.battery_current_deci / 10.0f : 0.0f;
        float yield = has_solar_data ? current_solar.record.solar.yield_today_centikwh / 100.0f : 0.0f;

        // PV Power - BIG (main value)
        snprintf(buf, sizeof(buf), "%4dW", pv_power);
        display_string_large(base_x + pad, y, buf, COLOR_GREEN, COLOR_BLACK);

        // State on right
        snprintf(buf, sizeof(buf), "%-8s", state);
        display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 34;

        // Power bar (0-450W)
        draw_mppt_power_bar(base_x + pad, y, bar_w, pv_power);
        y += 14;

        // Battery Current - BIG (main charging value)
        snprintf(buf, sizeof(buf), "%.1fA ", current);
        display_string_large(base_x + pad, y, buf, COLOR_CYAN, COLOR_BLACK);

        // Battery Voltage - smaller on right
        snprintf(buf, sizeof(buf), "%.2fV", voltage);
        display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 34;

        // Yield Today
        snprintf(buf, sizeof(buf), "Today: %.2f kWh    ", yield);
        display_string(base_x + pad, y, buf, COLOR_WHITE, COLOR_BLACK);
    }
    
    // === Q2: SMARTSHUNT (top-right) ===
    base_x = half_w;
    base_y = 0;
    display_fill_rect(base_x, base_y, half_w, half_h, COLOR_BLACK);
    display_string(base_x + pad, base_y + pad, "SMARTSHUNT", COLOR_YELLOW, COLOR_BLACK);
    display_string(base_x + half_w - pad - 32, base_y + pad, has_smartshunt_data ? "      " : "(--)", has_smartshunt_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);

    y = base_y + pad + 18;
    {
        float soc = has_smartshunt_data ? current_smartshunt.record.battery.soc_deci_percent / 10.0f : 0.0f;
        float voltage = has_smartshunt_data ? current_smartshunt.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float curr = has_smartshunt_data ? current_smartshunt.record.battery.battery_current_milli / 1000.0f : 0.0f;
        uint16_t ttg = has_smartshunt_data ? current_smartshunt.record.battery.time_to_go_minutes : 0;
        float consumed = has_smartshunt_data ? current_smartshunt.record.battery.consumed_ah_deci / -10.0f : 0.0f;

        // SOC - BIG
        snprintf(buf, sizeof(buf), "%.0f%% ", soc);
        uint16_t soc_color = get_soc_color(soc);
        display_string_large(base_x + pad, y, buf, has_smartshunt_data ? soc_color : COLOR_WHITE, COLOR_BLACK);

        // Voltage on right
        snprintf(buf, sizeof(buf), "%.2fV ", voltage);
        display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_CYAN, COLOR_BLACK);
        y += 34;

        // SOC Bar (0-100%)
        draw_smartshunt_soc_bar(base_x + pad, y, bar_w, has_smartshunt_data ? soc : 0.0f);
        y += 14;

        // Current - medium
        snprintf(buf, sizeof(buf), "%+.2fA   ", curr);
        uint16_t curr_color = get_current_color(curr);
        display_string_large(base_x + pad, y, buf, has_smartshunt_data ? curr_color : COLOR_WHITE, COLOR_BLACK);

        // TTG on right
        if (ttg != 0xFFFF && ttg > 0) {
            snprintf(buf, sizeof(buf), "TTG:%dh%02dm ", ttg/60, ttg%60);
        } else {
            snprintf(buf, sizeof(buf), "TTG:---    ");
        }
        display_string(base_x + pad + inner_w - 90, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 34;

        // Current Bar (-100A to +50A)
        draw_smartshunt_current_bar(base_x + pad, y, bar_w, has_smartshunt_data ? curr : 0.0f);
        y += 14;

        // Consumed
        snprintf(buf, sizeof(buf), "Used: %.1fAh         ", consumed);
        display_string(base_x + pad, y, buf, COLOR_WHITE, COLOR_BLACK);
    }
    
    // === Q3: BATTERY SENSE (bottom-left) ===
    base_x = 0;
    base_y = half_h;
    display_fill_rect(base_x, base_y, half_w, half_h, COLOR_BLACK);
    display_string(base_x + pad, base_y + pad, "BATTERY SENSE", COLOR_YELLOW, COLOR_BLACK);
    display_string(base_x + half_w - pad - 24, base_y + pad, has_battery_data ? "    " : "(--)", has_battery_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);

    y = base_y + pad + 18;
    {
        float voltage = has_battery_data ? current_battery.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float temp_k = has_battery_data ? current_battery.record.battery.aux_value / 100.0f : 273.15f;
        float temp_c = temp_k - 273.15f;

        bool temp_valid = has_battery_data && (current_battery.record.battery.aux_input == 2);
        if (!temp_valid && has_battery_data) {
            temp_c = 0.0f; // Fallback
        }

        // TEMPERATURE - BIG (main value)
        snprintf(buf, sizeof(buf), "%.1f C ", temp_c);
        uint16_t temp_color = get_battery_temp_color(temp_c);
        uint16_t temp_fg = has_battery_data ? temp_color : COLOR_WHITE;
        display_string_large(base_x + pad, y, buf, temp_fg, COLOR_BLACK);
        // Degree symbol workaround
        display_string(base_x + pad + 110, y, "o", temp_fg, COLOR_BLACK);
        y += 34;

        // Temperature bar (-10C to +50C)
        draw_battery_temp_bar(base_x + pad, y, bar_w, has_battery_data ? temp_c : 0.0f);
        y += 14;

        // VOLTAGE - medium size
        snprintf(buf, sizeof(buf), "%.2fV     ", voltage);
        display_string_large(base_x + pad, y, buf, COLOR_CYAN, COLOR_BLACK);
        y += 34;

        // Status line
        if (has_battery_data) {
            display_string(base_x + pad, y, "Battery OK              ", COLOR_GREEN, COLOR_BLACK);
        } else {
            display_string(base_x + pad, y, "No data                 ", COLOR_ORANGE, COLOR_BLACK);
        }
    }

    // === Q4: Placeholder (bottom-right) ===
    base_x = half_w;
    base_y = half_h;
    display_fill_rect(base_x, base_y, half_w, half_h, COLOR_BLACK);
    // Draw a simple border and label
    display_fill_rect(base_x, base_y, half_w, 2, COLOR_WHITE);
    display_fill_rect(base_x, base_y + half_h - 2, half_w, 2, COLOR_WHITE);
    display_fill_rect(base_x, base_y, 2, half_h, COLOR_WHITE);
    display_fill_rect(base_x + half_w - 2, base_y, 2, half_h, COLOR_WHITE);
    display_string(base_x + pad, base_y + pad, "Reserved", COLOR_YELLOW, COLOR_BLACK);

    xSemaphoreGive(data_mutex);
}

// Display update task
static void display_task(void *arg) {
    while (1) {
        draw_ui();
        vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Victron Solar Display (Hardcoded Keys) ===");
    
    // Create mutex
    data_mutex = xSemaphoreCreateMutex();
    
    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    display_init();
    display_fill(COLOR_BLACK);
    
    // Show startup screen
    display_string_large(40, 200, "VICTRON", COLOR_CYAN, COLOR_BLACK);
    display_string(80, 250, "Solar Display", COLOR_WHITE, COLOR_BLACK);
    display_string(50, 290, "Initializing BLE...", COLOR_YELLOW, COLOR_BLACK);
    
    // Initialize Victron BLE (keys are hardcoded)
    ESP_LOGI(TAG, "Initializing Victron BLE...");
    victron_ble_init();
    victron_ble_register_callback(victron_data_callback);
    
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    // Clear and start UI
    display_fill(COLOR_BLACK);
    
    // Start display task
    xTaskCreate(display_task, "display", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "System running. Waiting for Victron BLE data...");
}
