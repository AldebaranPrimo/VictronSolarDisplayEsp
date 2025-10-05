#include "relay_panel.h"

#include <lvgl.h>
#include <stdio.h>

#define RELAY_COL_COUNT 4
#define RELAY_MIN_BUTTON_SIZE 48

static lv_style_t relay_btn_on_style;
static lv_style_t relay_btn_off_style;
static bool relay_styles_init = false;

static void relay_button_event_cb(lv_event_t *e);
static void relay_apply_button_style(ui_state_t *ui, size_t index);
static void relay_destroy_button(ui_state_t *ui, size_t index);
static lv_coord_t relay_calc_button_size(ui_state_t *ui);

void ui_relay_panel_init(ui_state_t *ui)
{
    if (ui == NULL || ui->tab_relay == NULL) {
        return;
    }

    if (!relay_styles_init) {
        lv_style_init(&relay_btn_on_style);
        lv_style_set_bg_color(&relay_btn_on_style, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_text_color(&relay_btn_on_style, lv_color_white());
        lv_style_set_border_width(&relay_btn_on_style, 0);
        lv_style_set_radius(&relay_btn_on_style, 10);

        lv_style_init(&relay_btn_off_style);
        lv_style_set_bg_color(&relay_btn_off_style, lv_palette_darken(LV_PALETTE_GREY, 2));
        lv_style_set_text_color(&relay_btn_off_style, lv_color_white());
        lv_style_set_border_width(&relay_btn_off_style, 0);
        lv_style_set_radius(&relay_btn_off_style, 10);
        relay_styles_init = true;
    }

    lv_obj_t *description = lv_label_create(ui->tab_relay);
    lv_label_set_text(description,
                      "No relay buttons configured. Add them from Settings -> Relay controls.");
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(description, lv_pct(90));
    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 20);
    ui->relay_description = description;

    lv_obj_clear_flag(ui->tab_relay, LV_OBJ_FLAG_SCROLLABLE);

    ui->relay_grid = lv_obj_create(ui->tab_relay);
    lv_obj_remove_style_all(ui->relay_grid);
    lv_obj_set_size(ui->relay_grid, lv_pct(92), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(ui->relay_grid, 16, 0);
    lv_obj_set_style_pad_column(ui->relay_grid, 16, 0);
    lv_obj_set_style_pad_top(ui->relay_grid, 0, 0);
    lv_obj_set_style_pad_bottom(ui->relay_grid, 0, 0);
    lv_obj_set_style_pad_left(ui->relay_grid, 0, 0);
    lv_obj_set_style_pad_right(ui->relay_grid, 0, 0);
    lv_obj_set_layout(ui->relay_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->relay_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui->relay_grid,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ui->relay_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ui->relay_grid, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_flag(ui->relay_grid, LV_OBJ_FLAG_HIDDEN);

    ui_relay_panel_refresh(ui);
}

void ui_relay_panel_refresh(ui_state_t *ui)
{
    if (ui == NULL || ui->relay_grid == NULL) {
        return;
    }

    ui->relay_refresh_in_progress = true;

    if (!ui->relay_tab_enabled) {
        lv_obj_add_flag(ui->relay_grid, LV_OBJ_FLAG_HIDDEN);
        if (ui->relay_description != NULL) {
            lv_obj_clear_flag(ui->relay_description, LV_OBJ_FLAG_HIDDEN);
        }
        ui->relay_refresh_in_progress = false;
        return;
    }

    bool has_visible = false;
    lv_coord_t button_size = relay_calc_button_size(ui);

    for (size_t i = 0; i < ui->relay_config.count; ++i) {
        uint8_t pin = ui->relay_config.gpio_pins[i];
        if (pin == UI_RELAY_GPIO_UNASSIGNED) {
            relay_destroy_button(ui, i);
            ui->relay_button_state[i] = false;
            continue;
        }

        has_visible = true;

        lv_obj_t *btn = ui->relay_buttons[i];
        if (btn == NULL) {
            btn = lv_btn_create(ui->relay_grid);
            lv_obj_set_size(btn, button_size, button_size);
            lv_obj_add_event_cb(btn, relay_button_event_cb, LV_EVENT_CLICKED, ui);
            ui->relay_buttons[i] = btn;

            lv_obj_t *label = lv_label_create(btn);
            lv_obj_center(label);
            ui->relay_button_labels[i] = label;
        } else {
            lv_obj_set_size(btn, button_size, button_size);
        }

        lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *label = ui->relay_button_labels[i];
        if (label != NULL) {
            snprintf(ui->relay_button_text[i], sizeof(ui->relay_button_text[i]), "GPIO %u", (unsigned)pin);
            lv_label_set_text(label, ui->relay_button_text[i]);
        }

        relay_apply_button_style(ui, i);
    }

    for (size_t i = ui->relay_config.count; i < UI_MAX_RELAY_BUTTONS; ++i) {
        relay_destroy_button(ui, i);
        ui->relay_button_state[i] = false;
    }

    if (!has_visible) {
        lv_obj_add_flag(ui->relay_grid, LV_OBJ_FLAG_HIDDEN);
        if (ui->relay_description != NULL) {
            lv_obj_clear_flag(ui->relay_description, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_clear_flag(ui->relay_grid, LV_OBJ_FLAG_HIDDEN);
        if (ui->relay_description != NULL) {
            lv_obj_add_flag(ui->relay_description, LV_OBJ_FLAG_HIDDEN);
        }
    }

    ui->relay_refresh_in_progress = false;
}

static void relay_button_event_cb(lv_event_t *e)
{
    ui_state_t *ui = lv_event_get_user_data(e);
    if (ui == NULL || ui->relay_refresh_in_progress) {
        return;
    }

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    lv_obj_t *btn = lv_event_get_target(e);
    if (btn == NULL) {
        return;
    }

    for (size_t i = 0; i < UI_MAX_RELAY_BUTTONS; ++i) {
        if (ui->relay_buttons[i] == btn) {
            ui->relay_button_state[i] = !ui->relay_button_state[i];
            relay_apply_button_style(ui, i);
            break;
        }
    }
}

static void relay_apply_button_style(ui_state_t *ui, size_t index)
{
    if (ui == NULL || index >= UI_MAX_RELAY_BUTTONS) {
        return;
    }

    lv_obj_t *btn = ui->relay_buttons[index];
    if (btn == NULL) {
        return;
    }

    lv_obj_remove_style(btn, &relay_btn_on_style, LV_PART_MAIN);
    lv_obj_remove_style(btn, &relay_btn_off_style, LV_PART_MAIN);

    lv_obj_t *label = ui->relay_button_labels[index];

    if (ui->relay_button_state[index]) {
        lv_obj_add_style(btn, &relay_btn_on_style, LV_PART_MAIN);
        if (label != NULL) {
            lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    } else {
        lv_obj_add_style(btn, &relay_btn_off_style, LV_PART_MAIN);
        if (label != NULL) {
            lv_obj_set_style_text_color(label, lv_palette_darken(LV_PALETTE_GREY, 4), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

static void relay_destroy_button(ui_state_t *ui, size_t index)
{
    if (ui == NULL || index >= UI_MAX_RELAY_BUTTONS) {
        return;
    }

    if (ui->relay_buttons[index] != NULL) {
        lv_obj_del(ui->relay_buttons[index]);
        ui->relay_buttons[index] = NULL;
    }

    ui->relay_button_labels[index] = NULL;
}

static lv_coord_t relay_calc_button_size(ui_state_t *ui)
{
    if (ui == NULL || ui->relay_grid == NULL) {
        return RELAY_MIN_BUTTON_SIZE;
    }

    lv_coord_t grid_width = lv_obj_get_width(ui->relay_grid);
    if (grid_width <= 0 && ui->tab_relay != NULL) {
        lv_obj_update_layout(ui->tab_relay);
        grid_width = lv_obj_get_width(ui->relay_grid);
        if (grid_width <= 0) {
            grid_width = lv_obj_get_width(ui->tab_relay) * 92 / 100;
        }
    }

    lv_coord_t spacing = lv_obj_get_style_pad_column(ui->relay_grid, LV_PART_MAIN);
    if (spacing < 0) {
        spacing = 0;
    }

    lv_coord_t button_size = (grid_width - spacing * (RELAY_COL_COUNT - 1)) / RELAY_COL_COUNT;
    if (button_size < RELAY_MIN_BUTTON_SIZE) {
        button_size = RELAY_MIN_BUTTON_SIZE;
    }

    return button_size;
}
