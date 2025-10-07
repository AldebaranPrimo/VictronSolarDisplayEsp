/* victron_ble.c - Fixed version with debug logging */
#include "victron_ble.h"
#include "config_storage.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "aes/esp_aes.h"

static const char *TAG = "victron_ble";

// NA (Not Available) values per spec
#define NA_U16_SIGNED   0x7FFF
#define NA_U16_UNSIGNED 0xFFFF
#define NA_U8           0xFF
#define NA_U9           0x1FF
#define NA_U10          0x3FF
#define NA_U22          0x3FFFFF

// AES key buffer (loaded from NVS or default)
static uint8_t aes_key[16];

// Manufacturer-data layout
typedef struct __attribute__((packed)) {
    uint16_t vendorID;
    uint8_t  beaconType;
    uint8_t  unknownData1[3];
    uint8_t  victronRecordType;
    uint16_t nonceDataCounter;
    uint8_t  encryptKeyMatch;
    uint8_t  victronEncryptedData[21];
    uint8_t  nullPad;
} victronManufacturerData;

static victron_data_cb_t data_cb = NULL;
void victron_ble_register_callback(victron_data_cb_t cb) { data_cb = cb; }

// Forward declarations
static void ble_host_task(void *param);
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_app_on_sync(void);
static bool parse_solar_payload(const uint8_t *buf, size_t len, victron_solar_data_t *out);
static bool parse_battery_payload(const uint8_t *buf, size_t len, victron_battery_data_t *out);
static inline uint16_t read_u16_le(const uint8_t *buf);
static inline int32_t sign_extend(uint32_t value, uint8_t bits);

void victron_ble_init(void) {
    ESP_LOGI(TAG, "Initializing NVS for Victron BLE");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing and reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Load AES key from NVS, fallback to default
    if (load_aes_key(aes_key) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded AES key from NVS");
    } else {
        ESP_LOGW(TAG, "No user AES key found in NVS, using default");
        const uint8_t default_key[16] = {
            0x4B,0x71,0x78,0xE6, 0x4C,0x82,0x8A,0x26,
            0x2C,0xDD,0x51,0x61, 0xE3,0x40,0x4B,0x7A
        };
        memcpy(aes_key, default_key, sizeof(aes_key));
    }
    ESP_LOGI(TAG, "Using AES key:");
    ESP_LOG_BUFFER_HEX(TAG, aes_key, sizeof(aes_key));

    // Initialize BLE stack
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_app_on_sync(void) {
    struct ble_gap_disc_params disc_params = {
        .itvl = 0x0060, .window = 0x0030,
        .passive = 1, .filter_policy = 0, .limited = 0
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ble_gap_event_handler, NULL);
    if (rc) {
        ESP_LOGE(TAG, "Error starting discovery; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Started Victron BLE scan");
    }
}

static const char* get_device_type_name(uint8_t type) {
    switch(type) {
        case 0x00: return "Test Record";
        case 0x01: return "Solar Charger";
        case 0x02: return "Battery Monitor";
        case 0x03: return "Inverter";
        case 0x04: return "DC/DC Converter";
        case 0x05: return "SmartLithium";
        case 0x06: return "Inverter RS";
        case 0x07: return "GX-Device";
        case 0x08: return "AC Charger";
        case 0x09: return "Smart Battery Protect";
        case 0x0A: return "Lynx Smart BMS";
        case 0x0B: return "Multi RS";
        case 0x0C: return "VE.Bus";
        case 0x0D: return "DC Energy Meter";
        default: return "Unknown/Reserved";
    }
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    
    if (rc != 0) {
        ESP_LOGV(TAG, "Failed to parse advertisement fields, rc=%d", rc);
        return 0;
    }
    
    // Check if we have manufacturer data
    if (fields.mfg_data_len < offsetof(victronManufacturerData, victronEncryptedData) + 1) {
        return 0;
    }
    
    victronManufacturerData *mdata = (void*)fields.mfg_data;
    
    // Log all Victron packets (vendor ID 0x02e1)
    if (mdata->vendorID == 0x02e1) {
        ESP_LOGI(TAG, "=== Victron BLE Packet Received ===");
        ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 event->disc.addr.val[5], event->disc.addr.val[4],
                 event->disc.addr.val[3], event->disc.addr.val[2],
                 event->disc.addr.val[1], event->disc.addr.val[0]);
        ESP_LOGI(TAG, "Vendor ID: 0x%04X", mdata->vendorID);
        ESP_LOGI(TAG, "Beacon Type: 0x%02X", mdata->beaconType);
        ESP_LOGI(TAG, "Record Type: 0x%02X (%s)", mdata->victronRecordType, 
                 get_device_type_name(mdata->victronRecordType));
        ESP_LOGI(TAG, "Nonce/Counter: 0x%04X (%u)", mdata->nonceDataCounter, mdata->nonceDataCounter);
        ESP_LOGI(TAG, "Encrypt Key Match Byte: 0x%02X (local key[0]=0x%02X)", 
                 mdata->encryptKeyMatch, aes_key[0]);
        ESP_LOGI(TAG, "Manufacturer data length: %d bytes", fields.mfg_data_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, fields.mfg_data, fields.mfg_data_len, ESP_LOG_INFO);
    }
    
    // Check vendor ID and key match
    if (mdata->vendorID != 0x02e1) {
        return 0;
    }
    
    if (mdata->encryptKeyMatch != aes_key[0]) {
        ESP_LOGW(TAG, "Key mismatch! Device key[0]=0x%02X, our key[0]=0x%02X - SKIPPING DECRYPT",
                 mdata->encryptKeyMatch, aes_key[0]);
        return 0;
    }

    int encr_size = fields.mfg_data_len - offsetof(victronManufacturerData, victronEncryptedData);
    ESP_LOGI(TAG, "Encrypted data size: %d bytes", encr_size);
    
    uint8_t input[32] = {0}, output[32] = {0};
    memcpy(input, mdata->victronEncryptedData, encr_size);
    
    ESP_LOGI(TAG, "Encrypted payload:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, input, encr_size, ESP_LOG_INFO);

    // Decrypt
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    if (esp_aes_setkey(&ctx, aes_key, 128)) {
        ESP_LOGE(TAG, "AES setkey failed"); 
        esp_aes_free(&ctx); 
        return 0;
    }
    
    uint16_t nonce = mdata->nonceDataCounter;
    uint8_t ctr_blk[16] = { nonce & 0xFF, nonce >> 8 };
    uint8_t stream_block[16] = {0}; 
    size_t offset = 0;
    
    rc = esp_aes_crypt_ctr(&ctx, encr_size, &offset, ctr_blk, stream_block, input, output);
    esp_aes_free(&ctx);
    
    if (rc) { 
        ESP_LOGE(TAG, "AES CTR decrypt failed, rc=%d", rc); 
        return 0; 
    }

    ESP_LOGI(TAG, "Decrypted payload (nonce=0x%04X):", nonce);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, output, encr_size, ESP_LOG_INFO);

    victron_data_t parsed = {
        .type = VICTRON_DEVICE_TYPE_UNKNOWN,
    };

    bool parse_success = false;
    switch (mdata->victronRecordType) {
    case VICTRON_DEVICE_TYPE_SOLAR_CHARGER:
        ESP_LOGI(TAG, "Parsing as Solar Charger...");
        parse_success = parse_solar_payload(output, (size_t)encr_size, &parsed.payload.solar);
        if (parse_success) {
            parsed.type = VICTRON_DEVICE_TYPE_SOLAR_CHARGER;
            ESP_LOGI(TAG, "Solar Charger parsed successfully");
        } else {
            ESP_LOGE(TAG, "Failed to parse Solar Charger payload");
        }
        break;
        
    case VICTRON_DEVICE_TYPE_BATTERY_MONITOR:
        ESP_LOGI(TAG, "Parsing as Battery Monitor...");
        parse_success = parse_battery_payload(output, (size_t)encr_size, &parsed.payload.battery);
        if (parse_success) {
            parsed.type = VICTRON_DEVICE_TYPE_BATTERY_MONITOR;
            ESP_LOGI(TAG, "Battery Monitor parsed successfully");
        } else {
            ESP_LOGE(TAG, "Failed to parse Battery Monitor payload");
        }
        break;
        
    default:
        ESP_LOGW(TAG, "Unsupported record type 0x%02X (%s) - packet ignored",
                 mdata->victronRecordType, get_device_type_name(mdata->victronRecordType));
        return 0;
    }

    if (!parse_success) {
        ESP_LOGE(TAG, "=== Parsing Failed ===");
        return 0;
    }

    ESP_LOGI(TAG, "=== Packet Successfully Decoded ===\n");
    ui_set_ble_mac(event->disc.addr.val);

    if (data_cb) data_cb(&parsed);
    return 0;
}

static bool parse_solar_payload(const uint8_t *buf, size_t len, victron_solar_data_t *out)
{
    // Solar Charger: bits 32-160 = 16 bytes (not 12!)
    // buf[0-1]: device_state, error_code
    // buf[2-3]: battery_voltage (16 bits)
    // buf[4-5]: battery_current (16 bits)
    // buf[6-7]: yield_today (16 bits)
    // buf[8-9]: pv_power (16 bits)
    // buf[10-11]: load_current (9 bits, bits 112-120)
    if (len < 2 || out == NULL) {
        ESP_LOGE(TAG, "Solar payload too short: %zu bytes (need at least 2)", len);
        return false;
    }

    out->device_state = buf[0];
    out->error_code = buf[1];

    uint16_t voltage_raw = 0, current_raw = 0, yield_raw = 0, power_raw = 0, load_raw = 0;
    // Parse battery voltage if present
    if (len >= 4) voltage_raw = read_u16_le(&buf[2]);
    // Parse battery current if present
    if (len >= 6) current_raw = read_u16_le(&buf[4]);
    // Parse yield today if present
    if (len >= 8) yield_raw = read_u16_le(&buf[6]);
    // Parse PV power if present
    if (len >= 10) power_raw = read_u16_le(&buf[8]);
    // Parse load current if present
    if (len >= 12) load_raw = (uint16_t)buf[10] | ((uint16_t)(buf[11] & 0x01) << 8);

    ESP_LOGI(TAG, "  Device State: 0x%02X", out->device_state);
    ESP_LOGI(TAG, "  Error Code: 0x%02X", out->error_code);
    ESP_LOGI(TAG, "  Battery Voltage (raw): 0x%04X %s", voltage_raw, 
             (voltage_raw == NA_U16_SIGNED) ? "(NA)" : "");
    ESP_LOGI(TAG, "  Battery Current (raw): 0x%04X %s", current_raw,
             (current_raw == NA_U16_SIGNED) ? "(NA)" : "");
    ESP_LOGI(TAG, "  Yield Today (raw): 0x%04X %s", yield_raw,
             (yield_raw == NA_U16_UNSIGNED) ? "(NA)" : "");
    ESP_LOGI(TAG, "  PV Power (raw): 0x%04X %s", power_raw,
             (power_raw == NA_U16_UNSIGNED) ? "(NA)" : "");

    // Check for NA values and convert signed values
    out->battery_voltage_centi = (voltage_raw == NA_U16_SIGNED) ? 0 : (int16_t)voltage_raw;
    out->battery_current_deci = (current_raw == NA_U16_SIGNED) ? 0 : (int16_t)current_raw;
    out->today_yield_centikwh = (yield_raw == NA_U16_UNSIGNED) ? 0 : yield_raw;
    out->input_power_w = (power_raw == NA_U16_UNSIGNED) ? 0 : power_raw;

    ESP_LOGI(TAG, "  Load Current (raw): 0x%03X %s", load_raw,
             (load_raw == NA_U9) ? "(NA)" : "");
    out->load_current_deci = (load_raw == NA_U9) ? 0 : load_raw;

    ESP_LOGI(TAG, "  --> Battery: %.2fV, %.1fA", 
             out->battery_voltage_centi / 100.0, out->battery_current_deci / 10.0);
    ESP_LOGI(TAG, "  --> PV Power: %u W, Yield: %.2f kWh", 
             out->input_power_w, out->today_yield_centikwh / 100.0);
    ESP_LOGI(TAG, "  --> Load: %.1fA", out->load_current_deci / 10.0);

    return true;
}

static bool parse_battery_payload(const uint8_t *buf, size_t len, victron_battery_data_t *out)
{
    // Battery Monitor: bits 32-160 = 16 bytes
    // buf[0-1]: TTG (16 bits)
    // buf[2-3]: battery_voltage (16 bits)
    // buf[4-5]: alarm_reason (16 bits)
    // buf[6-7]: aux_value (16 bits)
    // buf[8-15]: packed bit fields (64 bits)
    if (len < 16 || out == NULL) {
        ESP_LOGE(TAG, "Battery payload too short: %zu bytes (need 16+)", len);
        return false;
    }

    uint16_t ttg_raw = read_u16_le(&buf[0]);
    uint16_t voltage_raw = read_u16_le(&buf[2]);
    uint16_t alarm_raw = read_u16_le(&buf[4]);
    uint16_t aux_raw = read_u16_le(&buf[6]);

    ESP_LOGI(TAG, "  TTG (raw): 0x%04X %s", ttg_raw,
             (ttg_raw == NA_U16_UNSIGNED) ? "(NA)" : "");
    ESP_LOGI(TAG, "  Battery Voltage (raw): 0x%04X %s", voltage_raw,
             (voltage_raw == NA_U16_SIGNED) ? "(NA)" : "");
    ESP_LOGI(TAG, "  Alarm Reason: 0x%04X", alarm_raw);
    ESP_LOGI(TAG, "  Aux Value (raw): 0x%04X", aux_raw);

    out->time_to_go_minutes = (ttg_raw == NA_U16_UNSIGNED) ? 0 : ttg_raw;
    out->battery_voltage_centi = (voltage_raw == NA_U16_SIGNED) ? 0 : voltage_raw;
    out->alarm_reason = alarm_raw;
    out->aux_value = aux_raw;

    // Extract packed fields from bytes 8-15 (64 bits)
    // Bit layout (relative to bit 96 in full record):
    // bits 0-1: aux_input (2 bits)
    // bits 2-23: battery_current (22 bits, signed)
    // bits 24-43: consumed_ah (20 bits, signed)
    // bits 44-53: soc (10 bits)
    
    ESP_LOGI(TAG, "  Packed field bytes [8-15]:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, &buf[8], 8, ESP_LOG_INFO);
    
    uint64_t tail = 0;
    for (size_t i = 0; i < 8; ++i) {
        tail |= ((uint64_t)buf[8 + i]) << (8 * i);
    }
    ESP_LOGI(TAG, "  Packed 64-bit value: 0x%016llX", tail);

    uint8_t aux_input_raw = (uint8_t)(tail & 0x03u);
    tail >>= 2;

    uint32_t current_bits = (uint32_t)(tail & ((1u << 22) - 1u));
    tail >>= 22;

    uint32_t consumed_bits = (uint32_t)(tail & ((1u << 20) - 1u));
    tail >>= 20;

    uint32_t soc_bits = (uint32_t)(tail & ((1u << 10) - 1u));

    ESP_LOGI(TAG, "  Aux Input: %u (0=AuxV, 1=MidV, 2=Temp, 3=None)", aux_input_raw);
    ESP_LOGI(TAG, "  Battery Current (bits): 0x%06X %s", current_bits,
             (current_bits == NA_U22) ? "(NA)" : "");
    ESP_LOGI(TAG, "  Consumed Ah (bits): 0x%05X", consumed_bits);
    ESP_LOGI(TAG, "  SOC (bits): 0x%03X %s", soc_bits,
             (soc_bits == NA_U10) ? "(NA)" : "");

    out->aux_input = aux_input_raw;

    // Apply sign extension and check NA values
    out->battery_current_milli = (current_bits == NA_U22) ? 0 : sign_extend(current_bits, 22);
    out->consumed_ah_deci = sign_extend(consumed_bits, 20);
    out->soc_deci_percent = (soc_bits == NA_U10 || soc_bits > 1000u) ? 0 : (uint16_t)soc_bits;

    ESP_LOGI(TAG, "  --> Battery: %.2fV, %.3fA", 
             out->battery_voltage_centi / 100.0, out->battery_current_milli / 1000.0);
    ESP_LOGI(TAG, "  --> SOC: %.1f%%, Consumed: %.1f Ah", 
             out->soc_deci_percent / 10.0, -out->consumed_ah_deci / 10.0);
    ESP_LOGI(TAG, "  --> TTG: %u min (%.1f hours)", 
             out->time_to_go_minutes, out->time_to_go_minutes / 60.0);

    // Interpret aux_value based on aux_input
    switch(out->aux_input) {
        case 0:
            ESP_LOGI(TAG, "  --> Aux Voltage: %.2fV", aux_raw / 100.0);
            break;
        case 1:
            ESP_LOGI(TAG, "  --> Mid Voltage: %.2fV", aux_raw / 100.0);
            break;
        case 2:
            ESP_LOGI(TAG, "  --> Temperature: %.2f°C", aux_raw / 100.0);
            break;
        case 3:
            ESP_LOGI(TAG, "  --> Aux: None");
            break;
    }

    return true;
}

static inline uint16_t read_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline int32_t sign_extend(uint32_t value, uint8_t bits)
{
    if (bits == 0 || bits >= 32) {
        return (int32_t)value;
    }
    uint32_t shift = 32u - (uint32_t)bits;
    return (int32_t)((int32_t)(value << shift) >> shift);
}