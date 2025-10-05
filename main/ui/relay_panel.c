#include "relay_panel.h"

#include <lvgl.h>

void ui_relay_panel_init(ui_state_t *ui)
{
    if (ui == NULL || ui->tab_relay == NULL) {
        return;
    }

    lv_obj_t *title = lv_label_create(ui->tab_relay);
    lv_obj_add_style(title, &ui->styles.medium, 0);
    lv_label_set_text(title, "GPIO Relay Control");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 16);

    lv_obj_t *description = lv_label_create(ui->tab_relay);
    lv_label_set_text(description,
                      "Configure relay outputs from this tab. "
                      "Add controls here to toggle the GPIO pins that "
                      "drive your relays.");
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(description, lv_pct(90));
    lv_obj_align(description, LV_ALIGN_TOP_LEFT, 8, 48);
}
