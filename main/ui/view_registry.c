#include "view_registry.h"

#include "view_solar.h"
#include "view_battery.h"
#include "esp_log.h"

static const char *TAG = "UI_VIEW_REGISTRY";

static const ui_view_descriptor_t VIEW_DESCRIPTORS[] = {
    { VICTRON_BLE_RECORD_SOLAR_CHARGER, "0x01 Solar Charger", ui_solar_view_create },
    { VICTRON_BLE_RECORD_BATTERY_MONITOR, "0x02 Battery Monitor", ui_battery_view_create },
};

static size_t descriptor_count(void)
{
    return sizeof(VIEW_DESCRIPTORS) / sizeof(VIEW_DESCRIPTORS[0]);
}

static size_t type_to_index(victron_record_type_t type)
{
    return (size_t)type;
}

const ui_view_descriptor_t *ui_view_registry_find(victron_record_type_t type)
{
    size_t count = descriptor_count();
    for (size_t i = 0; i < count; ++i) {
        if (VIEW_DESCRIPTORS[i].type == type) {
            return &VIEW_DESCRIPTORS[i];
        }
    }
    return NULL;
}

ui_device_view_t *ui_view_registry_ensure(ui_state_t *ui,
                                          victron_record_type_t type,
                                          lv_obj_t *parent)
{
    if (ui == NULL) {
        return NULL;
    }

    size_t index = type_to_index(type);
    if (index >= UI_MAX_DEVICE_VIEWS) {
        return NULL;
    }

    ui_device_view_t *view = ui->views[index];
    if (view == NULL) {
        const ui_view_descriptor_t *desc = ui_view_registry_find(type);
        if (desc == NULL || desc->create == NULL || parent == NULL) {
            return NULL;
        }
        view = desc->create(ui, parent);
        if (view == NULL) {
            ESP_LOGE(TAG, "Failed to create view for type 0x%02X", (unsigned)type);
            return NULL;
        }
        ui->views[index] = view;
    }

    return view;
}

const char *ui_view_registry_name(victron_record_type_t type)
{
    const ui_view_descriptor_t *desc = ui_view_registry_find(type);
    return desc ? desc->name : "Unknown";
}
