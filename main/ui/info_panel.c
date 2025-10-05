#include "info_panel.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <lvgl.h>
#include "config_storage.h"
#include "config_server.h"
#include "display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_NAMESPACE "wifi"

static const char *TAG_INFO = "UI_INFO";
static const char *APP_VERSION = "1.1.1";

static void ta_event_cb(lv_event_t *e);
static void wifi_event_cb(lv_event_t *e);
static void ap_checkbox_event_cb(lv_event_t *e);
static void password_toggle_btn_event_cb(lv_event_t *e);
static void save_key_btn_event_cb(lv_event_t *e);
static void reboot_btn_event_cb(lv_event_t *e);
static void brightness_slider_event_cb(lv_event_t *e);
static void cb_screensaver_event_cb(lv_event_t *e);
static void slider_ss_brightness_event_cb(lv_event_t *e);
static void spinbox_ss_time_event_cb(lv_event_t *e);
static void spinbox_ss_time_increment_event_cb(lv_event_t *e);
static void spinbox_ss_time_decrement_event_cb(lv_event_t *e);
static void screensaver_timer_cb(lv_timer_t *timer);
static void screensaver_enable(ui_state_t *ui, bool enable);
static void screensaver_wake(ui_state_t *ui);

void ui_info_panel_init(ui_state_t *ui,
                        const char *default_ssid,
                        const char *default_pass,
                        uint8_t ap_enabled)
{
    if (ui == NULL || ui->tab_info == NULL) {
        return;
    }

    lv_obj_t *lbl_version = lv_label_create(ui->tab_info);
    lv_obj_add_style(lbl_version, &ui->styles.title, 0);
    lv_label_set_text_fmt(lbl_version, "Version: %s", APP_VERSION);
    lv_obj_align(lbl_version, LV_ALIGN_TOP_RIGHT, -8, 15);

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

    const char *ap_password = (default_pass && default_pass[0] != '\0') ? default_pass : DEFAULT_AP_PASSWORD;

    ui->wifi.password = lv_textarea_create(ui->tab_info);
    lv_textarea_set_password_mode(ui->wifi.password, true);
    lv_textarea_set_one_line(ui->wifi.password, true);
    lv_obj_set_width(ui->wifi.password, lv_pct(80));
    lv_textarea_set_text(ui->wifi.password, ap_password);
    lv_obj_align(ui->wifi.password, LV_ALIGN_TOP_LEFT, 8, 120);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_FOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_DEFOCUSED, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->wifi.password, ta_event_cb, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->wifi.password, wifi_event_cb, LV_EVENT_VALUE_CHANGED, ui);

    ui->wifi.password_toggle = lv_btn_create(ui->tab_info);
    lv_obj_align(ui->wifi.password_toggle, LV_ALIGN_TOP_LEFT, 240, 180);
    lv_obj_set_width(ui->wifi.password_toggle, lv_pct(15));
    lv_obj_add_event_cb(ui->wifi.password_toggle, password_toggle_btn_event_cb, LV_EVENT_CLICKED, ui);

    lv_obj_t *lbl_toggle = lv_label_create(ui->wifi.password_toggle);
    lv_label_set_text(lbl_toggle, "Show");
    lv_obj_center(lbl_toggle);

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
    lv_obj_align(ui->screensaver.spinbox_timeout, LV_ALIGN_TOP_LEFT, 48, 760);
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
}

void ui_info_panel_on_user_activity(ui_state_t *ui)
{
    screensaver_wake(ui);
}

void ui_info_panel_set_mac(ui_state_t *ui, const char *mac_str)
{
    if (ui == NULL || ui->ta_mac == NULL || mac_str == NULL) {
        return;
    }
    lv_textarea_set_text(ui->ta_mac, mac_str);
}

static void ta_event_cb(lv_event_t *e)
{
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
        lv_disp_t *disp = lv_disp_get_default();
        lv_coord_t screen_h = disp ? lv_disp_get_ver_res(disp) : LV_VER_RES;
        lv_coord_t kb_height = lv_obj_get_height(ui->keyboard);
        lv_coord_t available_h = screen_h - kb_height;
        if (available_h < screen_h / 3) {
            available_h = screen_h / 3;
        }
        lv_obj_update_layout(ui->tabview);
        lv_obj_set_height(ui->tabview, available_h);
        lv_obj_update_layout(ui->tabview);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        if (ta == NULL) {
            return;
        }
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_keyboard_set_textarea(ui->keyboard, NULL);
        lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_disp_t *disp = lv_disp_get_default();
        lv_coord_t screen_h = disp ? lv_disp_get_ver_res(disp) : LV_VER_RES;
        lv_obj_set_height(ui->tabview, screen_h);
        lv_obj_update_layout(ui->tabview);
        lv_indev_reset(NULL, ta);
    }
}

static void wifi_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_target(e);
    const char *txt = lv_textarea_get_text(ta);
    if (ui == NULL) {
        return;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        if (ta == ui->wifi.ssid) {
            nvs_set_str(h, "ssid", txt);
        } else if (ta == ui->wifi.password) {
            nvs_set_str(h, "password", txt);
        }
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG_INFO, "Wi-Fi config saved");
    } else {
        ESP_LOGE(TAG_INFO, "nvs_open failed: %s", esp_err_to_name(err));
    }
}

static void password_toggle_btn_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->wifi.password == NULL) {
        return;
    }

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    bool new_mode = !lv_textarea_get_password_mode(ui->wifi.password);
    lv_textarea_set_password_mode(ui->wifi.password, new_mode);

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != NULL) {
        lv_label_set_text(label, new_mode ? "Show" : "Hide");
    }
}

static void ap_checkbox_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    lv_obj_t *checkbox = lv_event_get_target(e);
    if (ui == NULL || checkbox == NULL) {
        return;
    }
    bool en = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_u8(h, "enabled", en);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG_INFO, "AP %s", en ? "enabled" : "disabled");
    } else {
        ESP_LOGE(TAG_INFO, "nvs_open failed: %s", esp_err_to_name(err));
    }

    if (en) {
        wifi_ap_init();
    } else {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err == ESP_OK) {
            ESP_LOGI(TAG_INFO, "Soft-AP stopped");
        } else {
            ESP_LOGE(TAG_INFO, "Failed to stop AP: %s", esp_err_to_name(stop_err));
        }
    }
}

static void save_key_btn_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->ta_key == NULL) {
        return;
    }
    const char *hex = lv_textarea_get_text(ui->ta_key);
    if (strlen(hex) != 32) {
        ESP_LOGE(TAG_INFO, "AES key must be 32 hex chars");
        return;
    }
    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        char tmp[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        key[i] = (uint8_t)strtol(tmp, NULL, 16);
    }
    if (save_aes_key(key) == ESP_OK) {
        ESP_LOGI(TAG_INFO, "AES key saved via UI");
    } else {
        ESP_LOGE(TAG_INFO, "Failed to save AES key");
    }
}

static void reboot_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG_INFO, "Reboot requested via UI");
    esp_restart();
}

static void brightness_slider_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL) {
        return;
    }
    int val = lv_slider_get_value(lv_event_get_target(e));
    ui->brightness = (uint8_t)val;
    bsp_display_brightness_set(val);
    save_brightness(ui->brightness);
    ESP_LOGI(TAG_INFO, "Brightness set to %d", val);
}

static void cb_screensaver_event_cb(lv_event_t *e)
{
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

static void slider_ss_brightness_event_cb(lv_event_t *e)
{
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

static void spinbox_ss_time_event_cb(lv_event_t *e)
{
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

static void spinbox_ss_time_increment_event_cb(lv_event_t *e)
{
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

static void spinbox_ss_time_decrement_event_cb(lv_event_t *e)
{
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

static void screensaver_enable(ui_state_t *ui, bool enable)
{
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

static void screensaver_timer_cb(lv_timer_t *timer)
{
    ui_state_t *ui = timer ? (ui_state_t *)timer->user_data : NULL;
    if (ui == NULL) {
        return;
    }
    if (ui->screensaver.enabled && !ui->screensaver.active) {
        bsp_display_brightness_set(ui->screensaver.brightness);
        ui->screensaver.active = true;
    }
}

static void screensaver_wake(ui_state_t *ui)
{
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
