/* victron_ble.c */
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

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    if (rc || fields.mfg_data_len < offsetof(victronManufacturerData, victronEncryptedData) + 1) {
        return 0;
    }
    victronManufacturerData *mdata = (void*)fields.mfg_data;
    //ESP_LOGV(TAG, "Received mfg data len=%d", fields.mfg_data_len);
    //ESP_LOG_BUFFER_HEX(TAG, fields.mfg_data, fields.mfg_data_len);

    if (mdata->vendorID != 0x02e1 ||
        mdata->encryptKeyMatch != aes_key[0]) {
        return 0;
    }

    int encr_size = fields.mfg_data_len - offsetof(victronManufacturerData, victronEncryptedData);
    uint8_t input[32] = {0}, output[32] = {0};
    memcpy(input, mdata->victronEncryptedData, encr_size);
    //ESP_LOGV(TAG, "Encrypted data:");
    //ESP_LOG_BUFFER_HEX(TAG, input, encr_size);

    esp_aes_context ctx;
    esp_aes_init(&ctx);
    if (esp_aes_setkey(&ctx, aes_key, 128)) {
        ESP_LOGE(TAG, "AES setkey failed"); esp_aes_free(&ctx); return 0;
    }
    uint16_t nonce = mdata->nonceDataCounter;
    uint8_t ctr_blk[16] = { nonce & 0xFF, nonce >> 8 };
    uint8_t stream_block[16] = {0}; size_t offset = 0;
    rc = esp_aes_crypt_ctr(&ctx, encr_size, &offset, ctr_blk, stream_block, input, output);
    esp_aes_free(&ctx);
    if (rc) { ESP_LOGE(TAG, "AES CTR decrypt failed"); return 0; }

    //ESP_LOGV(TAG, "Decrypted payload (nonce=0x%04X):", nonce);
    //ESP_LOG_BUFFER_HEX(TAG, output, encr_size);

    victron_data_t parsed = {
        .type = VICTRON_DEVICE_TYPE_UNKNOWN,
    };

    switch (mdata->victronRecordType) {
    case VICTRON_DEVICE_TYPE_SOLAR_CHARGER:
        if (!parse_solar_payload(output, (size_t)encr_size, &parsed.payload.solar)) {
            return 0;
        }
        parsed.type = VICTRON_DEVICE_TYPE_SOLAR_CHARGER;
        break;
    case VICTRON_DEVICE_TYPE_BATTERY_MONITOR:
        if (!parse_battery_payload(output, (size_t)encr_size, &parsed.payload.battery)) {
            return 0;
        }
        parsed.type = VICTRON_DEVICE_TYPE_BATTERY_MONITOR;
        break;
    default:
        return 0;
    }

    ui_set_ble_mac(event->disc.addr.val);

    if (data_cb) data_cb(&parsed);
    return 0;
}

static bool parse_solar_payload(const uint8_t *buf, size_t len, victron_solar_data_t *out)
{
    if (len < 12 || out == NULL) {
        return false;
    }

    out->device_state = buf[0];
    out->error_code = buf[1];
    out->battery_voltage_centi = (int16_t)read_u16_le(&buf[2]);
    out->battery_current_deci = (int16_t)read_u16_le(&buf[4]);
    out->today_yield_centikwh = read_u16_le(&buf[6]);
    out->input_power_w = read_u16_le(&buf[8]);

    if ((buf[11] & 0xFE) != 0xFE) {
        return false;
    }
    uint16_t load_raw = (uint16_t)buf[10] | ((uint16_t)(buf[11] & 0x01) << 8);
    out->load_current_deci = load_raw;

    return true;
}

static bool parse_battery_payload(const uint8_t *buf, size_t len, victron_battery_data_t *out)
{
    if (len < 16 || out == NULL) {
        return false;
    }

    out->time_to_go_minutes = read_u16_le(&buf[0]);
    out->battery_voltage_centi = read_u16_le(&buf[2]);
    out->alarm_reason = read_u16_le(&buf[4]);
    out->aux_value = read_u16_le(&buf[6]);

    uint64_t tail = 0;
    for (size_t i = 0; i < 8; ++i) {
        tail |= ((uint64_t)buf[8 + i]) << (8 * i);
    }

    out->aux_input = (uint8_t)(tail & 0x03u);
    tail >>= 2;

    uint32_t current_bits = (uint32_t)(tail & ((1u << 22) - 1u));
    tail >>= 22;

    uint32_t consumed_bits = (uint32_t)(tail & ((1u << 20) - 1u));
    tail >>= 20;

    uint32_t soc_bits = (uint32_t)(tail & ((1u << 10) - 1u));

    out->battery_current_milli = sign_extend(current_bits, 22);
    out->consumed_ah_deci = sign_extend(consumed_bits, 20);
    if (soc_bits > 1000u) {
        soc_bits = 1000u;
    }
    out->soc_deci_percent = (uint16_t)soc_bits;

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
