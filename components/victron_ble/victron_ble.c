#include "victron_ble.h"
#include "victron_records.h"
#include "victron_products.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "aes/esp_aes.h"

static const char *TAG = "victron_ble";

static bool victron_debug_enabled = false;
#define VDBG(fmt, ...) do { if (victron_debug_enabled) ESP_LOGI(TAG, fmt, ##__VA_ARGS__); } while(0)

#define NA_U16_SIGNED   0x7FFF
#define NA_U16_UNSIGNED 0xFFFF
#define NA_U8           0xFF
#define NA_U9           0x1FF
#define NA_U10          0x3FF
#define NA_U22          0x3FFFFF

// Hardcoded AES keys with MAC addresses for identification
// MAC address in reverse order (LSB first as received from BLE)
static uint8_t mac_mppt[6] = { 0xb5, 0x7d, 0xb4, 0x39, 0x56, 0xc1 };  // c1:56:39:b4:7d:b5
static uint8_t aes_key_mppt[16] = {
    0xf2, 0xdc, 0xc3, 0xba, 0x40, 0xed, 0xb8, 0xde,
    0x7e, 0x07, 0xd7, 0x63, 0x8f, 0x13, 0xf9, 0x71
};

static uint8_t mac_batt[6] = { 0x2b, 0x9e, 0xbd, 0x91, 0xb6, 0xc1 };  // c1:b6:91:bd:9e:2b
static uint8_t aes_key_batt[16] = {
    0xb7, 0xab, 0xe1, 0x9c, 0x00, 0x32, 0x40, 0xbe,
    0x9d, 0xae, 0x89, 0xb8, 0xc3, 0x72, 0xdd, 0x43
};

static uint8_t mac_smartshunt[6] = { 0x2e, 0x1b, 0x0c, 0xcf, 0x3c, 0xf9 };  // f9:3c:cf:0c:1b:2e
static uint8_t aes_key_smartshunt[16] = {
    0x4c, 0x1e, 0x3c, 0xcd, 0x3d, 0x89, 0x2d, 0xb1,
    0x3d, 0x7a, 0x43, 0x74, 0x0b, 0x7f, 0x10, 0x21
};

static uint8_t mac_charger[6] = { 0x00, 0x7b, 0xca, 0xfc, 0xa6, 0xe9 };  // e9:a6:fc:ca:7b:00
static uint8_t aes_key_charger[16] = {
    0x19, 0xef, 0xd0, 0xcf, 0x51, 0xbe, 0xfc, 0x3e,
    0x2e, 0x4a, 0x2b, 0x85, 0x84, 0x14, 0x4f, 0x2a
};

static uint8_t aes_key[16]; // Working key for current decrypt

typedef enum {
    VICTRON_MANUFACTURER_RECORD_PRODUCT_ADVERTISEMENT = 0x10,
} victron_manufacturer_record_type_t;

typedef struct __attribute__((packed)) {
    uint16_t vendorID;
    uint8_t  manufacturer_record_type;
    uint8_t  manufacturer_record_length;
    uint16_t product_id;
    uint8_t  victronRecordType;
    uint16_t nonceDataCounter;
    uint8_t  encryptKeyMatch;
    uint8_t  victronEncryptedData[VICTRON_ENCRYPTED_DATA_MAX_SIZE];
    uint8_t  nullPad;
} victronManufacturerData;

static victron_data_cb_t data_cb = NULL;
void victron_ble_register_callback(victron_data_cb_t cb) { data_cb = cb; }

static void ble_host_task(void *param);
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void ble_app_on_sync(void);

static inline int32_t sign_extend(uint32_t value, uint8_t bits)
{
    uint32_t shift = 32u - bits;
    return (int32_t)(value << shift) >> shift;
}

/* -------------------------------------------------------------------------- */
/*  Initialization                                                            */
/* -------------------------------------------------------------------------- */

void victron_ble_init(void)
{
    ESP_LOGI(TAG, "Initializing Victron BLE with hardcoded keys");

    // NVS init for BLE stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "MPPT key[0]=0x%02X, Battery key[0]=0x%02X", 
             aes_key_mppt[0], aes_key_batt[0]);

    ESP_LOGI(TAG, "Initializing NimBLE stack");
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}

void victron_ble_set_debug(bool enabled)
{
    victron_debug_enabled = enabled;
    ESP_LOGI(TAG, "Victron BLE debug set to %s", enabled ? "ENABLED" : "disabled");
}

/* -------------------------------------------------------------------------- */
/*  BLE Stack                                                                 */
/* -------------------------------------------------------------------------- */

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_app_on_sync(void)
{
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

/* -------------------------------------------------------------------------- */
/*  Utility                                                                   */
/* -------------------------------------------------------------------------- */

static const char* get_device_type_name(uint8_t type)
{
    switch (type) {
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
        default:   return "Unknown/Reserved";
    }
}

/* -------------------------------------------------------------------------- */
/*  GAP Event Handler                                                         */
/* -------------------------------------------------------------------------- */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC)
        return 0;

    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    if (rc != 0) {
        ESP_LOGV(TAG, "Failed to parse advertisement fields, rc=%d", rc);
        return 0;
    }

    if (fields.mfg_data_len < offsetof(victronManufacturerData, victronEncryptedData) + 1)
        return 0;

    victronManufacturerData *mdata = (void*)fields.mfg_data;
    if (mdata->vendorID != VICTRON_MANUFACTURER_ID)
        return 0;

    if (mdata->manufacturer_record_type != VICTRON_MANUFACTURER_RECORD_PRODUCT_ADVERTISEMENT) {
        VDBG("Skipping manufacturer record type 0x%02X",
             (unsigned)mdata->manufacturer_record_type);
        return 0;
    }

    uint16_t product_id = mdata->product_id;
    const char *product_name = victron_product_name(product_id);
    if (victron_debug_enabled) {
        if (product_name) {
            ESP_LOGI(TAG, "Product ID: 0x%04X (%s)", product_id, product_name);
        } else {
            ESP_LOGI(TAG, "Product ID: 0x%04X (unknown)", product_id);
        }
    }

    // Verbose packet log
    VDBG("=== Victron BLE Packet Received ===");
    VDBG("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
         event->disc.addr.val[5], event->disc.addr.val[4],
         event->disc.addr.val[3], event->disc.addr.val[2],
         event->disc.addr.val[1], event->disc.addr.val[0]);
    VDBG("Vendor ID: 0x%04X, Record: 0x%02X (%s)",
         mdata->vendorID, mdata->victronRecordType,
         get_device_type_name(mdata->victronRecordType));
    VDBG("Nonce: 0x%04X, KeyMatch: 0x%02X",
         mdata->nonceDataCounter, mdata->encryptKeyMatch);
    if (victron_debug_enabled)
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, fields.mfg_data, fields.mfg_data_len, ESP_LOG_INFO);

    // Select correct key based on MAC address
    const uint8_t *mac = event->disc.addr.val;
    victron_device_id_t device_id = VICTRON_DEVICE_UNKNOWN;
    
    if (memcmp(mac, mac_mppt, 6) == 0) {
        memcpy(aes_key, aes_key_mppt, 16);
        device_id = VICTRON_DEVICE_MPPT;
    } else if (memcmp(mac, mac_batt, 6) == 0) {
        memcpy(aes_key, aes_key_batt, 16);
        device_id = VICTRON_DEVICE_BATTERY_SENSE;
    } else if (memcmp(mac, mac_smartshunt, 6) == 0) {
        memcpy(aes_key, aes_key_smartshunt, 16);
        device_id = VICTRON_DEVICE_SMARTSHUNT;
    } else if (memcmp(mac, mac_charger, 6) == 0) {
        memcpy(aes_key, aes_key_charger, 16);
        device_id = VICTRON_DEVICE_AC_CHARGER;
        ESP_LOGI(TAG, "AC CHARGER detected - MAC: %02X:%02X:%02X:%02X:%02X:%02X",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    } else {
        ESP_LOGW(TAG, "Unknown Victron MAC: %02X:%02X:%02X:%02X:%02X:%02X - skipping",
            mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
        return 0;
    }

    int encr_size = fields.mfg_data_len - offsetof(victronManufacturerData, victronEncryptedData);
    if (encr_size <= 0 || encr_size > 25) {
        ESP_LOGW(TAG, "Invalid encrypted data size: %d", encr_size);
        return 0;
    }

    uint8_t input[VICTRON_ENCRYPTED_DATA_MAX_SIZE] = {0};
    uint8_t output[VICTRON_ENCRYPTED_DATA_MAX_SIZE] = {0};
    memcpy(input, mdata->victronEncryptedData, encr_size);

    if (victron_debug_enabled) {
        ESP_LOGI(TAG, "Encrypted payload:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, input, encr_size, ESP_LOG_INFO);
    }

    /* ---------------- AES CTR Decrypt ---------------- */
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    if (esp_aes_setkey(&ctx, aes_key, 128)) {
        ESP_LOGE(TAG, "AES setkey failed");
        esp_aes_free(&ctx);
        return 0;
    }

    // Only first 2 bytes used for Victron nonce; rest = zero
    uint16_t nonce = mdata->nonceDataCounter;
    uint8_t ctr_blk[16] = { (uint8_t)(nonce & 0xFF), (uint8_t)(nonce >> 8) };
    uint8_t stream_block[16] = {0};
    size_t offset = 0;

    rc = esp_aes_crypt_ctr(&ctx, encr_size, &offset, ctr_blk, stream_block, input, output);
    esp_aes_free(&ctx);
    if (rc) {
        ESP_LOGE(TAG, "AES CTR decrypt failed, rc=%d", rc);
        return 0;
    }

    if (victron_debug_enabled) {
        ESP_LOGI(TAG, "Decrypted payload (nonce=0x%04X):", nonce);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, output, encr_size, ESP_LOG_INFO);
    }

    const victron_record_type_t rec_type = (victron_record_type_t)mdata->victronRecordType;

    /* ---------------- Record Parsing ---------------- */
    switch (rec_type) {
        case VICTRON_BLE_RECORD_SOLAR_CHARGER: {
            const victron_record_solar_charger_t *r = (const victron_record_solar_charger_t *)output;

            uint16_t load_raw = (uint16_t)output[10] | ((uint16_t)(output[11] & 0x01) << 8);
            float load_current_A = (load_raw == 0x1FF) ? 0.0f : load_raw / 10.0f;

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_SOLAR_CHARGER,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.solar.device_state = r->device_state;
                parsed.record.solar.charger_error = r->charger_error;
                parsed.record.solar.battery_voltage_centi = r->battery_voltage_centi;
                parsed.record.solar.battery_current_deci = r->battery_current_deci;
                parsed.record.solar.yield_today_centikwh = r->yield_today_centikwh;
                parsed.record.solar.pv_power_w = r->pv_power_w;
                parsed.record.solar.load_current_deci = load_raw;
                data_cb(&parsed);
            }
            break;
        }

        case VICTRON_BLE_RECORD_BATTERY_MONITOR: {
            const uint8_t *b = output;

            uint16_t ttg_raw     = b[0] | (b[1] << 8);
            uint16_t voltage_raw = b[2] | (b[3] << 8);
            uint16_t alarm_raw   = b[4] | (b[5] << 8);
            uint16_t aux_raw     = b[6] | (b[7] << 8);

            uint64_t tail = 0;
            for (int i = 0; i < 7; i++)
                tail |= ((uint64_t)b[8 + i]) << (8 * i);

            uint8_t aux_input = tail & 0x03; tail >>= 2;
            int32_t current_bits  = sign_extend(tail & ((1u << 22) - 1u), 22); tail >>= 22;
            int32_t consumed_bits = sign_extend(tail & ((1u << 20) - 1u), 20); tail >>= 20;
            uint32_t soc_bits = tail & ((1u << 10) - 1u);

            float current_A   = current_bits / 1000.0f;

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_BATTERY_MONITOR,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.battery.time_to_go_minutes = ttg_raw;
                parsed.record.battery.battery_voltage_centi = voltage_raw;
                parsed.record.battery.alarm_reason = alarm_raw;
                parsed.record.battery.aux_value = aux_raw;
                parsed.record.battery.aux_input = aux_input;
                parsed.record.battery.battery_current_milli = current_bits;
                parsed.record.battery.consumed_ah_deci = consumed_bits;
                parsed.record.battery.soc_deci_percent = soc_bits;
                data_cb(&parsed);
            }
            break;
        }

        case VICTRON_BLE_RECORD_INVERTER: {
            if (encr_size < 11) {
                ESP_LOGW(TAG, "Inverter payload too short: %d", encr_size);
                break;
            }

            const uint8_t *b = output;
            uint8_t device_state = b[0];
            uint16_t alarm_reason = b[1] | (b[2] << 8);
            int16_t battery_voltage_centi = (int16_t)(b[3] | (b[4] << 8));
            uint16_t ac_apparent_power_va = b[5] | (b[6] << 8);

            uint32_t tail = (uint32_t)b[7]
                          | ((uint32_t)b[8] << 8)
                          | ((uint32_t)b[9] << 16)
                          | ((uint32_t)b[10] << 24);
            uint16_t ac_voltage_centi = (uint16_t)(tail & 0x7FFFu);
            uint16_t ac_current_deci = (uint16_t)((tail >> 15) & 0x7FFu);

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_INVERTER,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.inverter.device_state = device_state;
                parsed.record.inverter.alarm_reason = alarm_reason;
                parsed.record.inverter.battery_voltage_centi = battery_voltage_centi;
                parsed.record.inverter.ac_apparent_power_va = ac_apparent_power_va;
                parsed.record.inverter.ac_voltage_centi = ac_voltage_centi;
                parsed.record.inverter.ac_current_deci = ac_current_deci;
                data_cb(&parsed);
            }
            break;
        }

        case VICTRON_BLE_RECORD_DCDC_CONVERTER: {
            if (encr_size < 10) {
                ESP_LOGW(TAG, "DC/DC payload too short: %d", encr_size);
                break;
            }

            const uint8_t *b = output;
            uint8_t device_state = b[0];
            uint8_t charger_error = b[1];
            uint16_t input_voltage_centi = (uint16_t)(b[2] | (b[3] << 8));
            uint16_t output_voltage_centi = (uint16_t)(b[4] | (b[5] << 8));
            uint32_t off_reason = (uint32_t)b[6]
                                | ((uint32_t)b[7] << 8)
                                | ((uint32_t)b[8] << 16)
                                | ((uint32_t)b[9] << 24);

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_DCDC_CONVERTER,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.dcdc.device_state = device_state;
                parsed.record.dcdc.charger_error = charger_error;
                parsed.record.dcdc.input_voltage_centi = input_voltage_centi;
                parsed.record.dcdc.output_voltage_centi = output_voltage_centi;
                parsed.record.dcdc.off_reason = off_reason;
                data_cb(&parsed);
            }
            break;
        }

        case VICTRON_BLE_RECORD_SMART_LITHIUM: {
            if (encr_size < 16) {
                ESP_LOGW(TAG, "Smart Lithium payload too short: %d", encr_size);
                break;
            }

            const uint8_t *b = output;
            uint32_t bms_flags = (uint32_t)b[0]
                               | ((uint32_t)b[1] << 8)
                               | ((uint32_t)b[2] << 16)
                               | ((uint32_t)b[3] << 24);
            uint16_t error_flags = (uint16_t)(b[4] | (b[5] << 8));
            uint8_t cell_values[8];
            for (int i = 0; i < 8; ++i) {
                cell_values[i] = b[6 + i];
            }
            uint16_t packed_voltage = (uint16_t)(b[14] | (b[15] << 8));
            uint16_t battery_voltage_centi = (packed_voltage & 0x0FFFu);
            uint8_t balancer_status = (uint8_t)((packed_voltage >> 12) & 0x0Fu);
            uint8_t temperature_raw = (encr_size > 16) ? b[16] : 0;
            int temperature_c = (int)temperature_raw - 40;

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_SMART_LITHIUM,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.lithium.bms_flags = bms_flags;
                parsed.record.lithium.error_flags = error_flags;
                parsed.record.lithium.cell1_centi = cell_values[0];
                parsed.record.lithium.cell2_centi = cell_values[1];
                parsed.record.lithium.cell3_centi = cell_values[2];
                parsed.record.lithium.cell4_centi = cell_values[3];
                parsed.record.lithium.cell5_centi = cell_values[4];
                parsed.record.lithium.cell6_centi = cell_values[5];
                parsed.record.lithium.cell7_centi = cell_values[6];
                parsed.record.lithium.cell8_centi = cell_values[7];
                parsed.record.lithium.battery_voltage_centi = battery_voltage_centi;
                parsed.record.lithium.balancer_status = balancer_status;
                parsed.record.lithium.temperature_c = temperature_raw;
                data_cb(&parsed);
            }
            break;
        }

        case VICTRON_BLE_RECORD_AC_CHARGER: {
            if (encr_size < 11) {
                ESP_LOGW(TAG, "AC Charger payload too short: %d", encr_size);
                break;
            }

            const victron_record_ac_charger_t *r = (const victron_record_ac_charger_t *)output;

            ESP_LOGI(TAG, "=== AC Charger IP22 ===");
            ESP_LOGI(TAG, "State=%u Error=0x%02X Vbat1=%.2fV Ibat1=%.1fA Temp=%dC",
                     (unsigned)r->device_state,
                     (unsigned)r->charger_error,
                     r->battery_voltage_1_centi / 100.0f,
                     r->battery_current_1_deci / 10.0f,
                     (int)r->temperature_c);

            if (data_cb) {
                victron_data_t parsed = {
                    .type = VICTRON_BLE_RECORD_AC_CHARGER,
                    .product_id = product_id,
                    .device_id = device_id
                };
                parsed.record.ac_charger = *r;
                data_cb(&parsed);
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unsupported record type 0x%02X (%s)",
                     rec_type, get_device_type_name(rec_type));
            break;
    }

    return 0;
}
