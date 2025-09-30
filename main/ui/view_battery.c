#include "view_battery.h"

#include <stdlib.h>
#include <string.h>

#include "ui_format.h"

typedef enum {
    BATTERY_PRIMARY_VOLTAGE = 0,
    BATTERY_PRIMARY_CURRENT,
    BATTERY_PRIMARY_SOC,
    BATTERY_PRIMARY_COUNT
} battery_primary_label_t;

typedef enum {
    BATTERY_SECONDARY_TTG = 0,
    BATTERY_SECONDARY_CONSUMED,
    BATTERY_SECONDARY_AUX,
    BATTERY_SECONDARY_COUNT
} battery_secondary_label_t;

typedef struct {
    ui_device_view_t base;
    lv_obj_t *row_primary;
    lv_obj_t *row_secondary;
    lv_obj_t *primary_labels[BATTERY_PRIMARY_COUNT];
    lv_obj_t *secondary_labels[BATTERY_SECONDARY_COUNT];
} ui_battery_view_t;

static void battery_view_update(ui_device_view_t *view, const victron_data_t *data);
static void battery_view_show(ui_device_view_t *view);
static void battery_view_hide(ui_device_view_t *view);
static void battery_view_destroy(ui_device_view_t *view);

static void format_primary_voltage(lv_obj_t *label, const victron_data_t *data);
static void format_primary_current(lv_obj_t *label, const victron_data_t *data);
static void format_primary_soc(lv_obj_t *label, const victron_data_t *data);
static void format_secondary_ttg(lv_obj_t *label, const victron_data_t *data);
static void format_secondary_consumed(lv_obj_t *label, const victron_data_t *data);
static void format_secondary_aux(lv_obj_t *label, const victron_data_t *data);

static const ui_label_descriptor_t battery_primary_descriptors[BATTERY_PRIMARY_COUNT] = {
    { "battery_voltage", "Batt V", format_primary_voltage },
    { "battery_current", "Current", format_primary_current },
    { "battery_soc", "SOC", format_primary_soc },
};

static const ui_label_descriptor_t battery_secondary_descriptors[BATTERY_SECONDARY_COUNT] = {
    { "ttg", "TTG", format_secondary_ttg },
    { "consumed", "Consumed", format_secondary_consumed },
    { "aux", "Aux", format_secondary_aux },
};

static lv_obj_t *create_label_box(ui_state_t *ui, lv_obj_t *parent,
                                  const ui_label_descriptor_t *desc)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, lv_pct(30), 110);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_outline_width(box, 0, 0);

    lv_obj_t *header = lv_label_create(box);
    lv_label_set_text(header, desc->title ? desc->title : "");
    lv_obj_add_style(header, &ui->styles.title, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *value = lv_label_create(box);
    lv_label_set_text(value, "--");
    lv_obj_add_style(value, &ui->styles.medium, 0);
    lv_obj_align(value, LV_ALIGN_CENTER, 0, 10);

    return value;
}

ui_device_view_t *ui_battery_view_create(ui_state_t *ui, lv_obj_t *parent)
{
    if (ui == NULL || parent == NULL) {
        return NULL;
    }

    ui_battery_view_t *view = calloc(1, sizeof(*view));
    if (view == NULL) {
        return NULL;
    }

    view->base.ui = ui;
    view->base.root = lv_obj_create(parent);
    lv_obj_set_size(view->base.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(view->base.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->base.root, 0, 0);
    lv_obj_set_style_outline_width(view->base.root, 0, 0);
    lv_obj_set_style_pad_all(view->base.root, 12, 0);
    lv_obj_clear_flag(view->base.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(view->base.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(view->base.root, 18, 0);
    lv_obj_add_flag(view->base.root, LV_OBJ_FLAG_HIDDEN);

    view->row_primary = lv_obj_create(view->base.root);
    lv_obj_set_size(view->row_primary, lv_pct(100), 110);
    lv_obj_set_flex_flow(view->row_primary, LV_STYLE_PAD_ROW);
    lv_obj_set_flex_align(view->row_primary,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->row_primary, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(view->row_primary, lv_color_hex(0x223355), 0);
    lv_obj_set_style_border_width(view->row_primary, 0, 0);
    lv_obj_set_style_outline_width(view->row_primary, 0, 0);
    lv_obj_set_style_pad_all(view->row_primary, 12, 0);
    lv_obj_set_style_pad_column(view->row_primary, 16, 0);

    for (size_t i = 0; i < BATTERY_PRIMARY_COUNT; ++i) {
        view->primary_labels[i] = create_label_box(ui, view->row_primary,
                                                   &battery_primary_descriptors[i]);
    }

    view->row_secondary = lv_obj_create(view->base.root);
    lv_obj_set_size(view->row_secondary, lv_pct(100), 110);
    lv_obj_set_flex_flow(view->row_secondary, LV_STYLE_PAD_ROW);
    lv_obj_set_flex_align(view->row_secondary,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->row_secondary, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(view->row_secondary, lv_color_hex(0x223355), 0);
    lv_obj_set_style_border_width(view->row_secondary, 0, 0);
    lv_obj_set_style_outline_width(view->row_secondary, 0, 0);
    lv_obj_set_style_pad_all(view->row_secondary, 12, 0);
    lv_obj_set_style_pad_column(view->row_secondary, 16, 0);

    for (size_t i = 0; i < BATTERY_SECONDARY_COUNT; ++i) {
        view->secondary_labels[i] = create_label_box(ui, view->row_secondary,
                                                     &battery_secondary_descriptors[i]);
    }

    view->base.update = battery_view_update;
    view->base.show = battery_view_show;
    view->base.hide = battery_view_hide;
    view->base.destroy = battery_view_destroy;

    return &view->base;
}

static ui_battery_view_t *battery_view_from_base(ui_device_view_t *base)
{
    return (ui_battery_view_t *)base;
}

static void battery_view_update(ui_device_view_t *view, const victron_data_t *data)
{
    ui_battery_view_t *battery = battery_view_from_base(view);
    if (battery == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }

    for (size_t i = 0; i < BATTERY_PRIMARY_COUNT; ++i) {
        if (battery->primary_labels[i]) {
            battery_primary_descriptors[i].formatter(battery->primary_labels[i], data);
        }
    }

    for (size_t i = 0; i < BATTERY_SECONDARY_COUNT; ++i) {
        if (battery->secondary_labels[i]) {
            battery_secondary_descriptors[i].formatter(battery->secondary_labels[i], data);
        }
    }

    const victron_battery_data_t *b = &data->payload.battery;
    if (view->ui && view->ui->lbl_error) {
        if (b->alarm_reason == 0) {
            lv_label_set_text(view->ui->lbl_error, "");
        } else {
            lv_label_set_text_fmt(view->ui->lbl_error,
                                  "Alarm: 0x%04X",
                                  b->alarm_reason);
        }
    }
}

static void battery_view_show(ui_device_view_t *view)
{
    if (view && view->root) {
        lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void battery_view_hide(ui_device_view_t *view)
{
    if (view && view->root) {
        lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void battery_view_destroy(ui_device_view_t *view)
{
    if (view == NULL) {
        return;
    }
    if (view->root) {
        lv_obj_del(view->root);
        view->root = NULL;
    }
    free(view);
}

static void format_primary_voltage(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    ui_label_set_unsigned_fixed(label,
                                (unsigned)b->battery_voltage_centi,
                                100, 2, " V");
}

static void format_primary_current(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    int current_cent = ui_round_div_signed((int)b->battery_current_milli, 10);
    ui_label_set_signed_fixed(label, current_cent, 100, 2, " A");
}

static void format_primary_soc(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    ui_label_set_unsigned_fixed(label,
                                (unsigned)b->soc_deci_percent,
                                10, 1, " %");
}

static void format_secondary_ttg(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    uint16_t ttg = b->time_to_go_minutes;
    lv_label_set_text_fmt(label, "%uh %02um",
                          (unsigned)(ttg / 60), (unsigned)(ttg % 60));
}

static void format_secondary_consumed(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    ui_label_set_signed_fixed(label,
                              (int)b->consumed_ah_deci,
                              10, 1, " Ah");
}

static void format_secondary_aux(lv_obj_t *label, const victron_data_t *data)
{
    if (label == NULL || data == NULL ||
        data->type != VICTRON_DEVICE_TYPE_BATTERY_MONITOR) {
        return;
    }
    const victron_battery_data_t *b = &data->payload.battery;
    char aux_buf[32];
    ui_format_aux_value(b->aux_input, b->aux_value, aux_buf, sizeof(aux_buf));
    lv_label_set_text(label, aux_buf);
}
