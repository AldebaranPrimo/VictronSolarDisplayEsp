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

// Previous values for change detection
static bool ui_initialized = false;
static int prev_pv_power = -999;
static float prev_solar_voltage = -999;
static float prev_solar_current = -999;
static float prev_solar_yield = -999;
static uint8_t prev_solar_state = 0xFF;
static float prev_soc = -999;
static float prev_shunt_voltage = -999;
static float prev_shunt_current = -999;
static uint16_t prev_ttg = 0xFFFF;
static float prev_consumed = -999;
static float prev_bat_voltage = -999;
static float prev_bat_temp = -999;
static bool prev_has_solar = false;
static bool prev_has_battery = false;
static bool prev_has_shunt = false;

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

// Draw the main UI - optimized to update only changed values
static void draw_ui(void) {
    char buf[64];

    // Landscape layout: 4 quadranti (2x2)
    const int half_w = DISPLAY_WIDTH / 2;   // 240
    const int half_h = DISPLAY_HEIGHT / 2;  // 160
    const int pad = 8;
    const int inner_w = half_w - pad * 2;
    const int bar_w = inner_w - 4;

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    
    // First time initialization - draw static elements once
    if (!ui_initialized) {
        display_fill(COLOR_BLACK);
        
        // Draw all section headers (static)
        display_string(pad, pad, "MPPT SOLAR CHARGER", COLOR_YELLOW, COLOR_BLACK);
        display_string(half_w + pad, pad, "SMARTSHUNT", COLOR_YELLOW, COLOR_BLACK);
        display_string(pad, half_h + pad, "BATTERY SENSE", COLOR_YELLOW, COLOR_BLACK);
        display_string(half_w + pad, half_h + pad, "Reserved", COLOR_YELLOW, COLOR_BLACK);
        
        // Draw Q4 borders (static)
        display_fill_rect(half_w, half_h, half_w, 2, COLOR_WHITE);
        display_fill_rect(half_w, DISPLAY_HEIGHT - 2, half_w, 2, COLOR_WHITE);
        display_fill_rect(half_w, half_h, 2, half_h, COLOR_WHITE);
        display_fill_rect(DISPLAY_WIDTH - 2, half_h, 2, half_h, COLOR_WHITE);
        
        ui_initialized = true;
    }
    
    // === Q1: MPPT SOLAR CHARGER (top-left) ===
    int base_x = 0;
    int base_y = 0;
    
    // Only update status indicator if changed
    if (has_solar_data != prev_has_solar) {
        display_string(base_x + half_w - pad - 24, base_y + pad, has_solar_data ? "    " : "(--)", has_solar_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);
        prev_has_solar = has_solar_data;
    }

    int y = base_y + pad + 18;
    {
        int pv_power = has_solar_data ? current_solar.record.solar.pv_power_w : 0;
        uint8_t state = has_solar_data ? current_solar.record.solar.device_state : VIC_STATE_OFF;
        const char *state_str = get_state_string(state);
        float voltage = has_solar_data ? current_solar.record.solar.battery_voltage_centi / 100.0f : 0.0f;
        float current = has_solar_data ? current_solar.record.solar.battery_current_deci / 10.0f : 0.0f;
        float yield = has_solar_data ? current_solar.record.solar.yield_today_centikwh / 100.0f : 0.0f;

        // PV Power - only update if changed
        if (pv_power != prev_pv_power) {
            snprintf(buf, sizeof(buf), "%4dW", pv_power);
            display_string_large(base_x + pad, y, buf, COLOR_GREEN, COLOR_BLACK);
            draw_mppt_power_bar(base_x + pad, y + 34, bar_w, pv_power);
            prev_pv_power = pv_power;
        }

        // State - only update if changed
        if (state != prev_solar_state) {
            snprintf(buf, sizeof(buf), "%-8s", state_str);
            display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
            prev_solar_state = state;
        }
        y += 34 + 14;

        // Battery Current - only update if changed
        if (current != prev_solar_current) {
            snprintf(buf, sizeof(buf), "%.1fA ", current);
            display_string_large(base_x + pad, y, buf, COLOR_CYAN, COLOR_BLACK);
            prev_solar_current = current;
        }

        // Battery Voltage - only update if changed
        if (voltage != prev_solar_voltage) {
            snprintf(buf, sizeof(buf), "%.2fV", voltage);
            display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
            prev_solar_voltage = voltage;
        }
        y += 34;

        // Yield Today - only update if changed
        if (yield != prev_solar_yield) {
            snprintf(buf, sizeof(buf), "Today: %.2f kWh    ", yield);
            display_string(base_x + pad, y, buf, COLOR_WHITE, COLOR_BLACK);
            prev_solar_yield = yield;
        }
    }
    
    // === Q2: SMARTSHUNT (top-right) ===
    base_x = half_w;
    base_y = 0;
    
    // Only update status indicator if changed
    if (has_smartshunt_data != prev_has_shunt) {
        display_string(base_x + half_w - pad - 32, base_y + pad, has_smartshunt_data ? "      " : "(--)", has_smartshunt_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);
        prev_has_shunt = has_smartshunt_data;
    }

    y = base_y + pad + 18;
    {
        float soc = has_smartshunt_data ? current_smartshunt.record.battery.soc_deci_percent / 10.0f : 0.0f;
        float voltage = has_smartshunt_data ? current_smartshunt.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float curr = has_smartshunt_data ? current_smartshunt.record.battery.battery_current_milli / 1000.0f : 0.0f;
        uint16_t ttg = has_smartshunt_data ? current_smartshunt.record.battery.time_to_go_minutes : 0;
        float consumed = has_smartshunt_data ? current_smartshunt.record.battery.consumed_ah_deci / -10.0f : 0.0f;

        // SOC - only update if changed
        if (soc != prev_soc) {
            snprintf(buf, sizeof(buf), "%.0f%% ", soc);
            uint16_t soc_color = get_soc_color(soc);
            display_string_large(base_x + pad, y, buf, has_smartshunt_data ? soc_color : COLOR_WHITE, COLOR_BLACK);
            draw_smartshunt_soc_bar(base_x + pad, y + 34, bar_w, has_smartshunt_data ? soc : 0.0f);
            prev_soc = soc;
        }

        // Voltage - only update if changed
        if (voltage != prev_shunt_voltage) {
            snprintf(buf, sizeof(buf), "%.2fV ", voltage);
            display_string(base_x + pad + inner_w - 70, y + 8, buf, COLOR_CYAN, COLOR_BLACK);
            prev_shunt_voltage = voltage;
        }
        y += 34 + 14;

        // Current - only update if changed
        if (curr != prev_shunt_current) {
            snprintf(buf, sizeof(buf), "%+.2fA   ", curr);
            uint16_t curr_color = get_current_color(curr);
            display_string_large(base_x + pad, y, buf, has_smartshunt_data ? curr_color : COLOR_WHITE, COLOR_BLACK);
            draw_smartshunt_current_bar(base_x + pad, y + 34, bar_w, has_smartshunt_data ? curr : 0.0f);
            prev_shunt_current = curr;
        }

        // TTG - only update if changed
        if (ttg != prev_ttg) {
            if (ttg != 0xFFFF && ttg > 0) {
                snprintf(buf, sizeof(buf), "TTG:%dh%02dm ", ttg/60, ttg%60);
            } else {
                snprintf(buf, sizeof(buf), "TTG:---    ");
            }
            display_string(base_x + pad + inner_w - 90, y + 8, buf, COLOR_WHITE, COLOR_BLACK);
            prev_ttg = ttg;
        }
        y += 34 + 14;

        // Consumed - only update if changed
        if (consumed != prev_consumed) {
            snprintf(buf, sizeof(buf), "Used: %.1fAh         ", consumed);
            display_string(base_x + pad, y, buf, COLOR_WHITE, COLOR_BLACK);
            prev_consumed = consumed;
        }
    }
    
    // === Q3: BATTERY SENSE (bottom-left) ===
    base_x = 0;
    base_y = half_h;
    
    // Only update status indicator if changed
    if (has_battery_data != prev_has_battery) {
        display_string(base_x + half_w - pad - 24, base_y + pad, has_battery_data ? "    " : "(--)", has_battery_data ? COLOR_BLACK : COLOR_RED, COLOR_BLACK);
        prev_has_battery = has_battery_data;
    }

    y = base_y + pad + 18;
    {
        float voltage = has_battery_data ? current_battery.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float temp_k = has_battery_data ? current_battery.record.battery.aux_value / 100.0f : 273.15f;
        float temp_c = temp_k - 273.15f;

        bool temp_valid = has_battery_data && (current_battery.record.battery.aux_input == 2);
        if (!temp_valid && has_battery_data) {
            temp_c = 0.0f;
        }

        // TEMPERATURE - only update if changed
        if (temp_c != prev_bat_temp) {
            snprintf(buf, sizeof(buf), "%.1f C ", temp_c);
            uint16_t temp_color = get_battery_temp_color(temp_c);
            uint16_t temp_fg = has_battery_data ? temp_color : COLOR_WHITE;
            display_string_large(base_x + pad, y, buf, temp_fg, COLOR_BLACK);
            display_string(base_x + pad + 110, y, "o", temp_fg, COLOR_BLACK);
            draw_battery_temp_bar(base_x + pad, y + 34, bar_w, has_battery_data ? temp_c : 0.0f);
            prev_bat_temp = temp_c;
        }
        y += 34 + 14;

        // VOLTAGE - only update if changed
        if (voltage != prev_bat_voltage) {
            snprintf(buf, sizeof(buf), "%.2fV     ", voltage);
            display_string_large(base_x + pad, y, buf, COLOR_CYAN, COLOR_BLACK);
            prev_bat_voltage = voltage;
        }
        y += 34;

        // Status line - update only on data status change
        static bool last_bat_status = false;
        if (has_battery_data != last_bat_status) {
            if (has_battery_data) {
                display_string(base_x + pad, y, "Battery OK              ", COLOR_GREEN, COLOR_BLACK);
            } else {
                display_string(base_x + pad, y, "No data                 ", COLOR_ORANGE, COLOR_BLACK);
            }
            last_bat_status = has_battery_data;
        }
    }

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
