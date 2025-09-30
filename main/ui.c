/* ui.c */
#include "ui.h"
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <lvgl.h>
#include "lv_port.h"
#include "esp_log.h"
#include "victron_ble.h"
#include "nvs_flash.h"
#include "config_storage.h"
#include <stdio.h>
#include "ui/ui_state.h"
#include "ui/device_view.h"
#include "ui/view_registry.h"
#include "ui/info_panel.h"

// Font Awesome symbols (declared in main.c)
LV_FONT_DECLARE(font_awesome_solar_panel_40);
LV_FONT_DECLARE(font_awesome_bolt_40);

static const char *TAG_UI = "UI_MODULE";

static ui_state_t g_ui = {
    .brightness = 100,
    .current_device_type = VICTRON_DEVICE_TYPE_UNKNOWN,
};

// Forward declarations
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

    ui_info_panel_init(ui, default_ssid, default_pass, ap_enabled);

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

void ui_set_ble_mac(const uint8_t *mac) {
    // Format MAC as "XX:XX:XX:XX:XX:XX"
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    ui_state_t *ui = &g_ui;
    lvgl_port_lock(0);
    ui_info_panel_set_mac(ui, mac_str);
    lvgl_port_unlock();
}

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
    ui_info_panel_on_user_activity(ui);
}
