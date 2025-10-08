// victron_ble.h
#ifndef VICTRON_BLE_H
#define VICTRON_BLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VICTRON_DEVICE_TYPE_UNKNOWN = 0x00,
    VICTRON_DEVICE_TYPE_SOLAR_CHARGER = 0x01,
    VICTRON_DEVICE_TYPE_BATTERY_MONITOR = 0x02,
} victron_device_type_t;

typedef struct {
    uint8_t  device_state;
    uint8_t  error_code;
    int16_t  battery_voltage_centi;
    int16_t  battery_current_deci;
    uint16_t today_yield_centikwh;
    uint16_t input_power_w;
    uint16_t load_current_deci;
} victron_solar_data_t;

typedef struct {
    uint16_t time_to_go_minutes;
    uint16_t battery_voltage_centi;
    uint16_t alarm_reason;
    uint16_t aux_value;
    uint8_t  aux_input;
    int32_t  battery_current_milli;
    int32_t  consumed_ah_deci;
    uint16_t soc_deci_percent;
} victron_battery_data_t;

typedef struct {
    victron_device_type_t type;
    union {
        victron_solar_data_t   solar;
        victron_battery_data_t battery;
    } payload;
} victron_data_t;

extern void ui_set_ble_mac(const uint8_t *mac);
// Callback for receiving new Victron data frames
typedef void (*victron_data_cb_t)(const victron_data_t *data);

// Initialize BLE scanning and decryption for Victron SmartSolar
void victron_ble_init(void);

// Register a callback to be invoked with each decoded Victron frame
void victron_ble_register_callback(victron_data_cb_t cb);

// Enable or disable verbose/debug logging in victron BLE module
void victron_ble_set_debug(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // VICTRON_BLE_H
