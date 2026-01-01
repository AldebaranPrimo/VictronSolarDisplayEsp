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
    
    // Full screen layout: 320x480, each section ~160px
    // No header, no footer - maximum usable area
    #define Y_MPPT_TITLE   5
    #define Y_MPPT_DATA   22
    #define Y_LINE1      150
    #define Y_BSENSE_TITLE 155
    #define Y_BSENSE_DATA  172
    #define Y_LINE2      305
    #define Y_SS_TITLE   310
    #define Y_SS_DATA    327
    
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    
    // === SECTION 1: MPPT SOLAR CHARGER (~150px) ===
    display_string(5, Y_MPPT_TITLE, "MPPT SOLAR CHARGER", COLOR_YELLOW, COLOR_BLACK);
    if (!has_solar_data) {
        display_string(245, Y_MPPT_TITLE, "(--)", COLOR_RED, COLOR_BLACK);
    } else {
        display_string(245, Y_MPPT_TITLE, "    ", COLOR_BLACK, COLOR_BLACK);
    }
    
    int y = Y_MPPT_DATA;
    {
        int pv_power = has_solar_data ? current_solar.record.solar.pv_power_w : 0;
        const char *state = has_solar_data ? get_state_string(current_solar.record.solar.device_state) : "OFF";
        float voltage = has_solar_data ? current_solar.record.solar.battery_voltage_centi / 100.0f : 0.0f;
        float current = has_solar_data ? current_solar.record.solar.battery_current_deci / 10.0f : 0.0f;
        float yield = has_solar_data ? current_solar.record.solar.yield_today_centikwh / 100.0f : 0.0f;
        
        // PV Power - BIG (main value)
        snprintf(buf, sizeof(buf), "%4dW", pv_power);
        display_string_large(5, y, buf, COLOR_GREEN, COLOR_BLACK);
        
        // State on right
        snprintf(buf, sizeof(buf), "%-8s", state);
        display_string(140, y+8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 40;
        
        // Battery Current - BIG (main charging value)
        snprintf(buf, sizeof(buf), "%.1fA ", current);
        display_string_large(5, y, buf, COLOR_CYAN, COLOR_BLACK);
        
        // Battery Voltage - smaller on right
        snprintf(buf, sizeof(buf), "%.2fV", voltage);
        display_string(160, y+8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 40;
        
        // Yield Today
        snprintf(buf, sizeof(buf), "Today: %.2f kWh    ", yield);
        display_string(5, y, buf, COLOR_WHITE, COLOR_BLACK);
    }
    
    // Separator line
    display_fill_rect(5, Y_LINE1, 310, 2, COLOR_WHITE);
    
    // === SECTION 2: SMART BATTERY SENSE (~150px) ===
    // Shows only TEMPERATURE (big) and VOLTAGE (small)
    display_string(5, Y_BSENSE_TITLE, "BATTERY SENSE", COLOR_YELLOW, COLOR_BLACK);
    if (!has_battery_data) {
        display_string(200, Y_BSENSE_TITLE, "(--)", COLOR_RED, COLOR_BLACK);
    } else {
        display_string(200, Y_BSENSE_TITLE, "    ", COLOR_BLACK, COLOR_BLACK);
    }
    
    y = Y_BSENSE_DATA;
    {
        // SmartBatterySense only has temperature and voltage
        // Temperature is in aux_value (Kelvin * 100) when aux_input == 2
        float voltage = has_battery_data ? current_battery.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float temp_k = has_battery_data ? current_battery.record.battery.aux_value / 100.0f : 273.15f;
        float temp_c = temp_k - 273.15f;
        
        // Check if temperature is valid (aux_input should be 2 for temp)
        bool temp_valid = has_battery_data && (current_battery.record.battery.aux_input == 2);
        if (!temp_valid && has_battery_data) {
            temp_c = 0.0f; // Fallback
        }
        
        // TEMPERATURE - BIG (main value)
        snprintf(buf, sizeof(buf), "%.1f C ", temp_c);
        // Color based on temperature
        uint16_t temp_color = COLOR_GREEN;
        if (temp_c < 5) temp_color = COLOR_CYAN;      // Cold
        else if (temp_c > 40) temp_color = COLOR_RED;  // Hot
        else if (temp_c > 30) temp_color = COLOR_ORANGE; // Warm
        display_string_large(5, y, buf, has_battery_data ? temp_color : COLOR_WHITE, COLOR_BLACK);
        
        // Degree symbol workaround
        display_string(115, y, "o", has_battery_data ? temp_color : COLOR_WHITE, COLOR_BLACK);
        
        y += 45;
        
        // VOLTAGE - medium size
        snprintf(buf, sizeof(buf), "%.2fV     ", voltage);
        display_string_large(5, y, buf, COLOR_CYAN, COLOR_BLACK);
        y += 45;
        
        // Status line
        if (has_battery_data) {
            display_string(5, y, "Battery OK              ", COLOR_GREEN, COLOR_BLACK);
        } else {
            display_string(5, y, "No data                 ", COLOR_ORANGE, COLOR_BLACK);
        }
    }
    
    // Separator line  
    display_fill_rect(5, Y_LINE2, 310, 2, COLOR_WHITE);
    
    // === SECTION 3: SMARTSHUNT (~165px) ===
    display_string(5, Y_SS_TITLE, "SMARTSHUNT", COLOR_YELLOW, COLOR_BLACK);
    if (!has_smartshunt_data) {
        display_string(180, Y_SS_TITLE, "  (--)  ", COLOR_RED, COLOR_BLACK);
    } else {
        display_string(180, Y_SS_TITLE, "        ", COLOR_BLACK, COLOR_BLACK);
    }
    
    y = Y_SS_DATA;
    {
        float soc = has_smartshunt_data ? current_smartshunt.record.battery.soc_deci_percent / 10.0f : 0.0f;
        float voltage = has_smartshunt_data ? current_smartshunt.record.battery.battery_voltage_centi / 100.0f : 0.0f;
        float curr = has_smartshunt_data ? current_smartshunt.record.battery.battery_current_milli / 1000.0f : 0.0f;
        uint16_t ttg = has_smartshunt_data ? current_smartshunt.record.battery.time_to_go_minutes : 0;
        float consumed = has_smartshunt_data ? current_smartshunt.record.battery.consumed_ah_deci / -10.0f : 0.0f;
        
        // SOC - BIG
        snprintf(buf, sizeof(buf), "%.0f%% ", soc);
        uint16_t soc_color = COLOR_GREEN;
        if (soc < 20) soc_color = COLOR_RED;
        else if (soc < 50) soc_color = COLOR_YELLOW;
        display_string_large(5, y, buf, has_smartshunt_data ? soc_color : COLOR_WHITE, COLOR_BLACK);
        
        // Voltage on right
        snprintf(buf, sizeof(buf), "%.2fV ", voltage);
        display_string(140, y+8, buf, COLOR_CYAN, COLOR_BLACK);
        y += 40;
        
        // Current - medium
        snprintf(buf, sizeof(buf), "%+.2fA   ", curr);
        display_string_large(5, y, buf, curr >= 0 ? COLOR_GREEN : COLOR_ORANGE, COLOR_BLACK);
        
        // TTG on right
        if (ttg != 0xFFFF && ttg > 0) {
            snprintf(buf, sizeof(buf), "TTG:%dh%02dm ", ttg/60, ttg%60);
        } else {
            snprintf(buf, sizeof(buf), "TTG:---    ");
        }
        display_string(180, y+8, buf, COLOR_WHITE, COLOR_BLACK);
        y += 40;
        
        // Consumed
        snprintf(buf, sizeof(buf), "Used: %.1fAh         ", consumed);
        display_string(5, y, buf, COLOR_WHITE, COLOR_BLACK);
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
