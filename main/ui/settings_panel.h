#ifndef UI_SETTINGS_PANEL_H
#define UI_SETTINGS_PANEL_H

#include <stdint.h>
#include "ui_state.h"

void ui_settings_panel_init(ui_state_t *ui,
                            const char *default_ssid,
                            const char *default_pass,
                            uint8_t ap_enabled);

void ui_settings_panel_on_user_activity(ui_state_t *ui);
void ui_settings_panel_set_mac(ui_state_t *ui, const char *mac_str);

#endif /* UI_SETTINGS_PANEL_H */
