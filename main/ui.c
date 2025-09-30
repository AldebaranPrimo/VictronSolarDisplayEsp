/* ui.c */
#include "ui.h"
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "esp_log.h"
#include "victron_ble.h"
#include "nvs_flash.h"
#include "config_storage.h"
#include "config_server.h"
#include "esp_wifi.h"
#include <stdio.h>
#include "ui/ui_state.h"
#include "ui/device_view.h"
#include "ui/view_registry.h"

// NVS namespace for Wi-Fi
#define WIFI_NAMESPACE "wifi"

// Font Awesome symbols (declared in main.c)
LV_FONT_DECLARE(font_awesome_solar_panel_40);
LV_FONT_DECLARE(font_awesome_bolt_40);

static const char *TAG_UI = "UI_MODULE";

static ui_state_t g_ui = {
    .brightness = 100,
    .current_device_type = VICTRON_DEVICE_TYPE_UNKNOWN,
};

// Forward declarations
static void ta_event_cb(lv_event_t *e);
static void brightness_slider_event_cb(lv_event_t *e);
static void wifi_event_cb(lv_event_t *e);
static void ap_checkbox_event_cb(lv_event_t *e);
static void save_key_btn_event_cb(lv_event_t *e);
static void reboot_btn_event_cb(lv_event_t *e);
static void screensaver_timer_cb(lv_timer_t *timer);
static void screensaver_enable(ui_state_t *ui, bool enable);
static void screensaver_wake(ui_state_t *ui);

// Forward declarations for screensaver UI event callbacks
static void cb_screensaver_event_cb(lv_event_t *e);
static void slider_ss_brightness_event_cb(lv_event_t *e);
static void spinbox_ss_time_event_cb(lv_event_t *e);
static void spinbox_ss_time_increment_event_cb(lv_event_t *e);
static void spinbox_ss_time_decrement_event_cb(lv_event_t *e);
// Forward declarations (already present, just for clarity)
static void tabview_touch_event_cb(lv_event_t *e);
static void ensure_device_layout(ui_state_t *ui, victron_device_type_t type);
static const char *device_type_name(victron_device_type_t type);

void ui_init(void) {
    ui_state_t *ui = &g_ui;

    nvs_flash_init();
    load_brightness(&ui->brightness);

    ui->active_view = NULL;
    ui->current_device_type = VICTRON_DEVICE_TYPE_UNKNOWN;
    for (size_t i = 0; i < UI_MAX_DEVICE_VIEWS; ++i) {
        ui->views[i] = NULL;
    }

    char default_ssid[33]; size_t ssid_len = sizeof(default_ssid);
    char default_pass[65]; size_t pass_len = sizeof(default_pass);
    uint8_t ap_enabled;
    if (load_wifi_config(default_ssid, &ssid_len, default_pass, &pass_len, &ap_enabled) != ESP_OK) {
        strncpy(default_ssid, "VictronConfig", sizeof(default_ssid));
        default_ssid[sizeof(default_ssid) - 1] = '\0';
        default_pass[0] = '\0';
        ap_enabled = 1;
    }

    load_screensaver_settings(&ui->screensaver.enabled,
                              &ui->screensaver.brightness,
                              &ui->screensaver.timeout);

#if LV_USE_THEME_DEFAULT
    lv_theme_default_init(NULL,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK,
        &lv_font_montserrat_14
    );
#endif

    ui->tabview  = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 40);
    ui->tab_live = lv_tabview_add_tab(ui->tabview, "Live");
    ui->tab_info = lv_tabview_add_tab(ui->tabview, "Info");

    lv_obj_add_event_cb(ui->tab_live, tabview_touch_event_cb, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->tab_live, tabview_touch_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->tab_live, tabview_touch_event_cb, LV_EVENT_GESTURE, ui);

    lv_obj_add_event_cb(ui->tab_info, tabview_touch_event_cb, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->tab_info, tabview_touch_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->tab_info, tabview_touch_event_cb, LV_EVENT_GESTURE, ui);

    ui->keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(ui->keyboard, LV_HOR_RES, LV_VER_RES/2);
    lv_obj_align(ui->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);

    // Styles
    lv_style_init(&ui->styles.title);
    lv_style_set_text_font(&ui->styles.title, &lv_font_montserrat_16);
    lv_style_set_text_color(&ui->styles.title, lv_color_white());

    lv_style_init(&ui->styles.medium);
    lv_style_set_text_font(&ui->styles.medium, &lv_font_montserrat_24);
    lv_style_set_text_color(&ui->styles.medium, lv_color_white());

    lv_style_init(&ui->styles.big);
    lv_style_set_text_font(&ui->styles.big, &lv_font_montserrat_40);
    lv_style_set_text_color(&ui->styles.big, lv_color_white());

    lv_style_init(&ui->styles.value);
#if LV_FONT_MONTSERRAT_30
    lv_style_set_text_font(&ui->styles.value, &lv_font_montserrat_30);
#else
    lv_style_set_text_font(&ui->styles.value, LV_FONT_DEFAULT);
#endif
    lv_style_set_text_color(&ui->styles.value, lv_color_white());

    lv_obj_t *lbl_ssid = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_ssid, &ui->styles.title, 0);
    lv_label_set_text(lbl_ssid, "AP SSID:");
    lv_obj_align(lbl_ssid, LV_ALIGN_TOP_LEFT, 8, 15);

    ui->wifi.ssid = lv_textarea_create(ui->tab_info);
    lv_textarea_set_one_line(ui->wifi.ssid, true);
    lv_obj_set_width(ui->wifi.ssid, lv_pct(80));
    lv_textarea_set_text(ui->wifi.ssid, default_ssid);
    lv_obj_align(ui->wifi.ssid, LV_ALIGN_TOP_LEFT, 8, 45);
    lv_obj_add_event_cb(ui->wifi.ssid, ta_event_cb, LV_EVENT_FOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.ssid, ta_event_cb, LV_EVENT_DEFOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.ssid, ta_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->wifi.ssid, ta_event_cb, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->wifi.ssid, wifi_event_cb, LV_EVENT_VALUE_CHANGED, ui);

    lv_obj_t *lbl_pass = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_pass, &ui->styles.title, 0);
    lv_label_set_text(lbl_pass, "AP Password:");
    lv_obj_align(lbl_pass, LV_ALIGN_TOP_LEFT, 8, 90);

    ui->wifi.password = lv_textarea_create(ui->tab_info);
    lv_textarea_set_password_mode(ui->wifi.password, true);
    lv_textarea_set_one_line(ui->wifi.password, true);
    lv_obj_set_width(ui->wifi.password, lv_pct(80));
    lv_textarea_set_text(ui->wifi.password, default_pass);
    lv_obj_align(ui->wifi.password, LV_ALIGN_TOP_LEFT, 8, 120);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_FOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_DEFOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->wifi.password, wifi_event_cb, LV_EVENT_VALUE_CHANGED, ui);

    ui->wifi.ap_enable = lv_checkbox_create(ui->tab_info);
    lv_checkbox_set_text(ui->wifi.ap_enable, "Enable AP");
    lv_obj_add_style(ui->wifi.ap_enable, &ui->styles.medium, 0);
    if (ap_enabled) {
        lv_obj_add_state(ui->wifi.ap_enable, LV_STATE_CHECKED);
    }
    lv_obj_align(ui->wifi.ap_enable, LV_ALIGN_TOP_LEFT, 8, 180);
    lv_obj_add_event_cb(ui->wifi.ap_enable, ap_checkbox_event_cb, LV_EVENT_VALUE_CHANGED, ui);

    ui->lbl_error = lv_label_create(ui->tab_info);
    lv_obj_add_style(ui->lbl_error, &ui->styles.title, 0);
    lv_label_set_text(ui->lbl_error, "Err: 0");
    lv_obj_align(ui->lbl_error, LV_ALIGN_TOP_LEFT, 240, 820);

    ui->lbl_device_type = lv_label_create(ui->tab_info);
    lv_obj_add_style(ui->lbl_device_type, &ui->styles.title, 0);
    lv_label_set_text(ui->lbl_device_type, "Device: --");
    lv_obj_align(ui->lbl_device_type, LV_ALIGN_TOP_LEFT, 8, 820);

    lv_obj_t *lmac = lv_label_create(ui->tab_info);
    lv_obj_add_style(lmac, &ui->styles.title, 0);
    lv_label_set_text(lmac, "MAC Address:");
    lv_obj_align(lmac, LV_ALIGN_TOP_LEFT, 8, 250);

    ui->ta_mac = lv_textarea_create(ui->tab_info);
    lv_textarea_set_one_line(ui->ta_mac, true);
    lv_obj_set_width(ui->ta_mac, lv_pct(80));
    lv_textarea_set_text(ui->ta_mac, "00:00:00:00:00:00");
    lv_obj_align(ui->ta_mac, LV_ALIGN_TOP_LEFT, 8, 280);
    lv_obj_add_event_cb(ui->ta_mac, ta_event_cb, LV_EVENT_FOCUSED, ui);
    lv_obj_add_event_cb(ui->ta_mac, ta_event_cb, LV_EVENT_DEFOCUSED, ui);
    lv_obj_add_event_cb(ui->ta_mac, ta_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->ta_mac, ta_event_cb, LV_EVENT_READY, ui);

    lv_obj_t *lkey = lv_label_create(ui->tab_info);
    lv_obj_add_style(lkey, &ui->styles.title, 0);
    lv_label_set_text(lkey, "AES Key:");
    lv_obj_align(lkey, LV_ALIGN_TOP_LEFT, 8, 320);

    uint8_t aes_key_bin[16] = {0};
    char aes_key_hex[33] = {0};
    if (load_aes_key(aes_key_bin) == ESP_OK) {
        for (int i = 0; i < 16; ++i) {
            sprintf(aes_key_hex + i * 2, "%02X", aes_key_bin[i]);
        }
    } else {
        strcpy(aes_key_hex, "00000000000000000000000000000000");
    }

    ui->ta_key = lv_textarea_create(ui->tab_info);
    lv_textarea_set_one_line(ui->ta_key, true);
    lv_obj_set_width(ui->ta_key, lv_pct(80));
    lv_textarea_set_text(ui->ta_key, aes_key_hex);
    lv_obj_align(ui->ta_key, LV_ALIGN_TOP_LEFT, 8, 350);
    lv_obj_add_event_cb(ui->ta_key, ta_event_cb, LV_EVENT_FOCUSED, ui);
    lv_obj_add_event_cb(ui->ta_key, ta_event_cb, LV_EVENT_DEFOCUSED, ui);
    lv_obj_add_event_cb(ui->ta_key, ta_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->ta_key, ta_event_cb, LV_EVENT_READY, ui);

    lv_obj_t *btn_save_key = lv_btn_create(ui->tab_info);
    lv_obj_align(btn_save_key, LV_ALIGN_TOP_LEFT, 8, 400);
    lv_obj_set_width(btn_save_key, lv_pct(18));
    lv_obj_t *lbl_btn = lv_label_create(btn_save_key);
    lv_label_set_text(lbl_btn, "Save");
    lv_obj_center(lbl_btn);
    lv_obj_add_event_cb(btn_save_key, save_key_btn_event_cb, LV_EVENT_CLICKED, ui);

    lv_obj_t *btn_reboot = lv_btn_create(ui->tab_info);
    lv_obj_align(btn_reboot, LV_ALIGN_TOP_LEFT, 8 + lv_pct(20), 400);
    lv_obj_set_width(btn_reboot, lv_pct(18));
    lv_obj_t *lbl_reboot = lv_label_create(btn_reboot);
    lv_label_set_text(lbl_reboot, "Reboot");
    lv_obj_center(lbl_reboot);
    lv_obj_add_event_cb(btn_reboot, reboot_btn_event_cb, LV_EVENT_CLICKED, ui);

    lv_obj_t *lbl_brightness = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_brightness, &ui->styles.title, 0);
    lv_label_set_text(lbl_brightness, "Brightness:");
    lv_obj_align(lbl_brightness, LV_ALIGN_TOP_LEFT, 8, 450);

    lv_obj_t *slider = lv_slider_create(ui->tab_info);
    lv_obj_set_width(slider, lv_pct(80));
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 8, 500);
    lv_slider_set_range(slider, 1, 100);
    lv_slider_set_value(slider, ui->brightness, LV_ANIM_OFF);
    bsp_display_brightness_set(ui->brightness);
    lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, ui);
    lv_obj_add_style(slider, &ui->styles.medium, 0);

    lv_obj_t *spacer = lv_obj_create(ui->tab_info);
    lv_obj_set_size(spacer, 10, 40);
    lv_obj_align(spacer, LV_ALIGN_TOP_LEFT, 8, 550);
    lv_obj_add_flag(spacer, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_EVENT_BUBBLE);

    ui->screensaver.checkbox = lv_checkbox_create(ui->tab_info);
    lv_checkbox_set_text(ui->screensaver.checkbox, "Enable Screensaver");
    if (ui->screensaver.enabled) {
        lv_obj_add_state(ui->screensaver.checkbox, LV_STATE_CHECKED);
    }
    lv_obj_align(ui->screensaver.checkbox, LV_ALIGN_TOP_LEFT, 8, 600);
    lv_obj_add_event_cb(ui->screensaver.checkbox, cb_screensaver_event_cb, LV_EVENT_VALUE_CHANGED, ui);
    lv_obj_add_style(ui->screensaver.checkbox, &ui->styles.medium, 0);

    lv_obj_t *lbl_ss_brightness = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_ss_brightness, &ui->styles.title, 0);
    lv_label_set_text(lbl_ss_brightness, "Screensaver Brightness:");
    lv_obj_align(lbl_ss_brightness, LV_ALIGN_TOP_LEFT, 8, 650);

    ui->screensaver.slider_brightness = lv_slider_create(ui->tab_info);
    lv_obj_set_width(ui->screensaver.slider_brightness, lv_pct(80));
    lv_obj_align(ui->screensaver.slider_brightness, LV_ALIGN_TOP_LEFT, 8, 680);
    lv_slider_set_range(ui->screensaver.slider_brightness, 1, 100);
    lv_slider_set_value(ui->screensaver.slider_brightness, ui->screensaver.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui->screensaver.slider_brightness, slider_ss_brightness_event_cb, LV_EVENT_VALUE_CHANGED, ui);
    lv_obj_add_style(ui->screensaver.slider_brightness, &ui->styles.medium, 0);

    lv_obj_t *lbl_ss_time = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_ss_time, &ui->styles.title, 0);
    lv_label_set_text(lbl_ss_time, "Screensaver Timeout (s):");
    lv_obj_align(lbl_ss_time, LV_ALIGN_TOP_LEFT, 8, 730);

    ui->screensaver.spinbox_timeout = lv_spinbox_create(ui->tab_info);
    lv_spinbox_set_range(ui->screensaver.spinbox_timeout, 5, 3600);
    lv_spinbox_set_value(ui->screensaver.spinbox_timeout, ui->screensaver.timeout);
    lv_spinbox_set_digit_format(ui->screensaver.spinbox_timeout, 4, 0);
    lv_obj_set_width(ui->screensaver.spinbox_timeout, 100);
    lv_obj_align(ui->screensaver.spinbox_timeout, LV_ALIGN_TOP_LEFT, 8 + 40, 760);
    lv_obj_add_event_cb(ui->screensaver.spinbox_timeout, spinbox_ss_time_event_cb, LV_EVENT_VALUE_CHANGED, ui);

    lv_coord_t h = lv_obj_get_height(ui->screensaver.spinbox_timeout);

    lv_obj_t *btn_dec = lv_btn_create(ui->tab_info);
    lv_obj_set_size(btn_dec, h, h);
    lv_obj_align_to(btn_dec, ui->screensaver.spinbox_timeout, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_set_style_bg_img_src(btn_dec, LV_SYMBOL_MINUS, 0);
    lv_obj_add_event_cb(btn_dec, spinbox_ss_time_decrement_event_cb, LV_EVENT_ALL, ui);

    lv_obj_t *btn_inc = lv_btn_create(ui->tab_info);
    lv_obj_set_size(btn_inc, h, h);
    lv_obj_align_to(btn_inc, ui->screensaver.spinbox_timeout, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_bg_img_src(btn_inc, LV_SYMBOL_PLUS, 0);
    lv_obj_add_event_cb(btn_inc, spinbox_ss_time_increment_event_cb, LV_EVENT_ALL, ui);

    ui->screensaver.timer = lv_timer_create(screensaver_timer_cb,
                                            ui->screensaver.timeout * 1000,
                                            ui);
    if (ui->screensaver.enabled) {
        lv_timer_reset(ui->screensaver.timer);
        lv_timer_resume(ui->screensaver.timer);
    } else {
        lv_timer_pause(ui->screensaver.timer);
    }

    lv_obj_add_event_cb(lv_scr_act(), tabview_touch_event_cb, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(lv_scr_act(), tabview_touch_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(lv_scr_act(), tabview_touch_event_cb, LV_EVENT_GESTURE, ui);

    lv_obj_add_event_cb(ui->tabview, tabview_touch_event_cb, LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->tabview, tabview_touch_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->tabview, tabview_touch_event_cb, LV_EVENT_GESTURE, ui);

    lvgl_port_unlock();
}

void ui_on_panel_data(const victron_data_t *d) {
    if (d == NULL) {
        return;
    }

    ui_state_t *ui = &g_ui;

    lvgl_port_lock(0);

    const char *type_str = device_type_name(d->type);
    if (ui->lbl_device_type) {
        lv_label_set_text_fmt(ui->lbl_device_type, "Device: %s", type_str);
    }

    ensure_device_layout(ui, d->type);

    if (ui->active_view && ui->active_view->update) {
        ui->active_view->update(ui->active_view, d);
    } else if (ui->lbl_error) {
        if (d->type == VICTRON_DEVICE_TYPE_UNKNOWN) {
            lv_label_set_text(ui->lbl_error, "Unknown device type");
        } else {
            lv_label_set_text(ui->lbl_error, "No renderer for device type");
        }
    }

    lvgl_port_unlock();
}

static void ta_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (ui == NULL || ui->keyboard == NULL) {
        return;
    }

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(ui->keyboard, ta);
        lv_obj_move_foreground(ui->keyboard);
        lv_obj_clear_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(ui->keyboard, NULL);
        lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void brightness_slider_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    int val = lv_slider_get_value(lv_event_get_target(e));
    if (ui == NULL) {
        return;
    }
    ui->brightness = (uint8_t)val;
    bsp_display_brightness_set(val);
    save_brightness(ui->brightness);
    ESP_LOGI(TAG_UI, "Brightness set to %d", val);
}

static void wifi_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_target(e);
    const char *txt = lv_textarea_get_text(ta);
    if (ui == NULL) {
        return;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (ta == ui->wifi.ssid)
            nvs_set_str(h, "ssid", txt);
        else if (ta == ui->wifi.password)
            nvs_set_str(h, "password", txt);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG_UI, "Wi-Fi config saved");
    } else {
        ESP_LOGE(TAG_UI, "nvs_open failed: %s", esp_err_to_name(err));
    }
}


static void ap_checkbox_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    lv_obj_t *checkbox = lv_event_get_target(e);
    bool en = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_u8(h, "enabled", en);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG_UI, "AP %s", en ? "enabled" : "disabled");
    } else {
        ESP_LOGE(TAG_UI, "nvs_open failed: %s", esp_err_to_name(err));
    }

    if (en) {
        wifi_ap_init();
    } else {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err == ESP_OK) {
            ESP_LOGI(TAG_UI, "Soft-AP stopped");
        } else {
            ESP_LOGE(TAG_UI, "Failed to stop AP: %s", esp_err_to_name(stop_err));
        }
        // (optionally)
        // esp_wifi_deinit();
    }
}

static void save_key_btn_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->ta_key == NULL) {
        return;
    }
    const char *hex = lv_textarea_get_text(ui->ta_key);
    if (strlen(hex) != 32) {
        ESP_LOGE(TAG_UI, "AES key must be 32 hex chars");
        // Optionally show a message box or error label
        return;
    }
    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        char tmp[3] = { hex[i*2], hex[i*2+1], 0 };
        key[i] = (uint8_t)strtol(tmp, NULL, 16);
    }
    if (save_aes_key(key) == ESP_OK) {
        ESP_LOGI(TAG_UI, "AES key saved via UI");
        // Optionally show a success message
    } else {
        ESP_LOGE(TAG_UI, "Failed to save AES key");
        // Optionally show a failure message
    }
}

static void reboot_btn_event_cb(lv_event_t *e) {
    ESP_LOGI(TAG_UI, "Reboot requested via UI");
    esp_restart();
}

void ui_set_ble_mac(const uint8_t *mac) {
    // Format MAC as "XX:XX:XX:XX:XX:XX"
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    ui_state_t *ui = &g_ui;
    lvgl_port_lock(0);
    if (ui->ta_mac) {
        lv_textarea_set_text(ui->ta_mac, mac_str);
    }
    lvgl_port_unlock();
}

// --- Screensaver logic ---
static void screensaver_enable(ui_state_t *ui, bool enable) {
    if (ui == NULL || ui->screensaver.timer == NULL) {
        return;
    }

    if (enable) {
        ui->screensaver.active = false;
        bsp_display_brightness_set(ui->brightness);
        lv_timer_set_period(ui->screensaver.timer, ui->screensaver.timeout * 1000U);
        lv_timer_reset(ui->screensaver.timer);
        lv_timer_resume(ui->screensaver.timer);
    } else {
        lv_timer_pause(ui->screensaver.timer);
        if (ui->screensaver.active) {
            bsp_display_brightness_set(ui->brightness);
            ui->screensaver.active = false;
        }
    }
}

static void screensaver_timer_cb(lv_timer_t *timer) {
    ui_state_t *ui = timer ? (ui_state_t *)timer->user_data : NULL;
    if (ui == NULL) {
        return;
    }

    if (ui->screensaver.enabled && !ui->screensaver.active) {
        bsp_display_brightness_set(ui->screensaver.brightness);
        ui->screensaver.active = true;
    }
}

static void screensaver_wake(ui_state_t *ui) {
    if (ui == NULL || ui->screensaver.timer == NULL) {
        return;
    }

    if (ui->screensaver.enabled) {
        lv_timer_reset(ui->screensaver.timer);
        if (ui->screensaver.active) {
            bsp_display_brightness_set(ui->brightness);
            ui->screensaver.active = false;
        }
    }
}

// Screensaver UI event callbacks
static void cb_screensaver_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->screensaver.checkbox == NULL) {
        return;
    }

    ui->screensaver.enabled = lv_obj_has_state(ui->screensaver.checkbox, LV_STATE_CHECKED);
    save_screensaver_settings(ui->screensaver.enabled,
                              ui->screensaver.brightness,
                              ui->screensaver.timeout);
    screensaver_enable(ui, ui->screensaver.enabled);
}

static void slider_ss_brightness_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->screensaver.slider_brightness == NULL) {
        return;
    }

    ui->screensaver.brightness = lv_slider_get_value(ui->screensaver.slider_brightness);
    save_screensaver_settings(ui->screensaver.enabled,
                              ui->screensaver.brightness,
                              ui->screensaver.timeout);

    if (ui->screensaver.active) {
        bsp_display_brightness_set(ui->screensaver.brightness);
    }
}

static void spinbox_ss_time_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->screensaver.spinbox_timeout == NULL) {
        return;
    }

    ui->screensaver.timeout = lv_spinbox_get_value(ui->screensaver.spinbox_timeout);
    save_screensaver_settings(ui->screensaver.enabled,
                              ui->screensaver.brightness,
                              ui->screensaver.timeout);
    if (ui->screensaver.timer) {
        lv_timer_set_period(ui->screensaver.timer, ui->screensaver.timeout * 1000U);
    }
}

static void spinbox_ss_time_increment_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->screensaver.spinbox_timeout == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        lv_spinbox_increment(ui->screensaver.spinbox_timeout);
        ui->screensaver.timeout = lv_spinbox_get_value(ui->screensaver.spinbox_timeout);
        save_screensaver_settings(ui->screensaver.enabled,
                                  ui->screensaver.brightness,
                                  ui->screensaver.timeout);
        if (ui->screensaver.timer) {
            lv_timer_set_period(ui->screensaver.timer, ui->screensaver.timeout * 1000U);
        }
    }
}

static void spinbox_ss_time_decrement_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->screensaver.spinbox_timeout == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        lv_spinbox_decrement(ui->screensaver.spinbox_timeout);
        ui->screensaver.timeout = lv_spinbox_get_value(ui->screensaver.spinbox_timeout);
        save_screensaver_settings(ui->screensaver.enabled,
                                  ui->screensaver.brightness,
                                  ui->screensaver.timeout);
        if (ui->screensaver.timer) {
            lv_timer_set_period(ui->screensaver.timer, ui->screensaver.timeout * 1000U);
        }
    }
}

// Add this callback implementation at file scope:
static void ensure_device_layout(ui_state_t *ui, victron_device_type_t type)
{
    if (ui == NULL) {
        return;
    }

    if (type == ui->current_device_type) {
        return;
    }

    if (ui->active_view && ui->active_view->hide) {
        ui->active_view->hide(ui->active_view);
    }

    ui->active_view = NULL;

    ui_device_view_t *view = ui_view_registry_ensure(ui, type, ui->tab_live);
    if (view && view->show) {
        view->show(view);
        ui->active_view = view;
    } else if (type != VICTRON_DEVICE_TYPE_UNKNOWN) {
        ESP_LOGW(TAG_UI, "No view available for device type 0x%02X", (unsigned)type);
    }

    ui->current_device_type = type;
}

static const char *device_type_name(victron_device_type_t type)
{
    return ui_view_registry_name(type);
}

static void tabview_touch_event_cb(lv_event_t *e) {
    ui_state_t *ui = lv_event_get_user_data(e);
    screensaver_wake(ui);
}
