/**
 * UI Bars - Color-coded progress bars for Victron data visualization
 * Solution 2: Discrete Zones with multi-threshold colors
 */
#ifndef UI_BARS_H
#define UI_BARS_H

#include <stdint.h>
#include "simple_display.h"

/**
 * @brief Draw an MPPT Power bar (0-450W)
 * Colors: White/Gray [0-50W] → Green [50-200W] → Yellow [200-300W] → Red [300-450W]
 */
void draw_mppt_power_bar(int x, int y, int power_w);

/**
 * @brief Draw a Battery Sense Temperature bar (-10°C to +50°C)
 * Colors: Red [-10-0°C] → Yellow [0-10°C] → Green [10-30°C] → Yellow [30-40°C] → Red [>40°C]
 */
void draw_battery_temp_bar(int x, int y, float temp_c);

/**
 * @brief Draw a SmartShunt SOC bar (0-100%)
 * Colors: Red [0-20%] → Yellow [20-50%] → Green [50-80%] → Green bright [80-100%]
 */
void draw_smartshunt_soc_bar(int x, int y, float soc_percent);

/**
 * @brief Draw a SmartShunt Current bar (-100A to +50A)
 * Colors: Red [-100 to -20A] → Yellow [-20-0A] → White [0-10A] → Green [10-30A] → Yellow [>30A]
 */
void draw_smartshunt_current_bar(int x, int y, float current_a);

/**
 * @brief Get color for MPPT power
 */
uint16_t get_mppt_color(int power_w);

/**
 * @brief Get color for battery temperature
 */
uint16_t get_battery_temp_color(float temp_c);

/**
 * @brief Get color for SmartShunt SOC
 */
uint16_t get_soc_color(float soc_percent);

/**
 * @brief Get color for SmartShunt current
 */
uint16_t get_current_color(float current_a);

#endif // UI_BARS_H
