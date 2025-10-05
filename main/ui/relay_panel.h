#ifndef UI_RELAY_PANEL_H
#define UI_RELAY_PANEL_H

#include "ui_state.h"

/**
 * Initialize widgets used for GPIO relay control.
 */
void ui_relay_panel_init(ui_state_t *ui);

/**
 * Update relay tab contents to reflect configured GPIO assignments.
 */
void ui_relay_panel_refresh(ui_state_t *ui);

#endif /* UI_RELAY_PANEL_H */
