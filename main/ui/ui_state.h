#ifndef UI_UI_STATE_H
#define UI_UI_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>
#include "victron_ble.h"

struct ui_device_view;

#define UI_MAX_DEVICE_VIEWS 3

typedef struct {
    lv_style_t title;
    lv_style_t value;
    lv_style_t big;
    lv_style_t medium;
} ui_styles_t;

typedef struct {
    lv_obj_t *ssid;
    lv_obj_t *password;
    lv_obj_t *ap_enable;
    lv_obj_t *password_toggle;
} ui_wifi_controls_t;

typedef struct {
    bool enabled;
    uint8_t brightness;
    uint16_t timeout;
    bool active;
    lv_timer_t *timer;
    lv_obj_t *checkbox;
    lv_obj_t *slider_brightness;
    lv_obj_t *spinbox_timeout;
} ui_screensaver_state_t;

typedef struct ui_state {
    lv_obj_t *tabview;
    lv_obj_t *tab_live;
    lv_obj_t *tab_settings;
    lv_obj_t *tab_relay;
    lv_obj_t *keyboard;
    ui_styles_t styles;
    ui_wifi_controls_t wifi;
    ui_screensaver_state_t screensaver;
    lv_obj_t *lbl_error;
    lv_obj_t *lbl_device_type;
    lv_obj_t *lbl_no_data;
    lv_obj_t *ta_mac;
    lv_obj_t *ta_key;
    uint8_t brightness;
    victron_device_type_t current_device_type;
    struct ui_device_view *active_view;
    struct ui_device_view *views[UI_MAX_DEVICE_VIEWS];
    bool has_received_data;
    lv_obj_t *relay_checkbox;
    uint16_t tab_settings_index;
    uint16_t tab_relay_index;
    bool relay_tab_enabled;
} ui_state_t;

#endif /* UI_UI_STATE_H */
