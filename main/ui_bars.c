/**
 * UI Bars - Implementation of color-coded progress bars
 */
#include "ui_bars.h"
#include "simple_display.h"
#include <math.h>

// ============================================================================
// COLOR SELECTION FUNCTIONS
// ============================================================================

/**
 * Get color for MPPT power (0-450W)
 * White/Gray: 0-50W
 * Green: 50-200W
 * Yellow: 200-300W
 * Red: 300-450W+
 */
uint16_t get_mppt_color(int power_w) {
    if (power_w < 50) return 0x8410;      // Gray
    if (power_w < 200) return COLOR_GREEN; // Green
    if (power_w < 300) return COLOR_YELLOW; // Yellow
    return COLOR_RED;                       // Red
}

/**
 * Get color for Battery Sense Temperature (-10°C to +50°C)
 * Red: -10 to 0°C
 * Yellow: 0 to 10°C
 * Green: 10 to 30°C
 * Yellow: 30 to 40°C
 * Red: >40°C
 */
uint16_t get_battery_temp_color(float temp_c) {
    if (temp_c < 0) return COLOR_RED;      // Red (cold warning)
    if (temp_c < 10) return COLOR_YELLOW;  // Yellow (cool)
    if (temp_c < 30) return COLOR_GREEN;   // Green (optimal)
    if (temp_c < 40) return COLOR_YELLOW;  // Yellow (warm)
    return COLOR_RED;                       // Red (hot warning)
}

/**
 * Get color for SmartShunt SOC (0-100%)
 * Red: 0-20%
 * Yellow: 20-50%
 * Green: 50-80%
 * Green bright: 80-100% (same as green but could be lighter if needed)
 */
uint16_t get_soc_color(float soc_percent) {
    if (soc_percent < 20) return COLOR_RED;     // Red (critical)
    if (soc_percent < 50) return COLOR_YELLOW;  // Yellow (low)
    if (soc_percent < 80) return COLOR_GREEN;   // Green (ok)
    return 0x07E0;                               // Green (full) - could use 0x2FE0 for lighter green
}

/**
 * Get color for SmartShunt Current (-100A to +50A)
 * Violet: -100 to -30A (discharging very heavily)
 * Red: -30 to -10A (discharging heavily)
 * Yellow: -10 to -1A (discharging lightly)
 * White: -1 to +10A (idle or charging slowly)
 * Green: +10 to +30A (charging normally)
 * Yellow: +30A+ (charging fast)
 */
uint16_t get_current_color(float current_a) {
    if (current_a < -30) return COLOR_MAGENTA;  // Violet (very heavy discharge)
    if (current_a < -10) return COLOR_RED;      // Red (heavy discharge)
    if (current_a < -1) return COLOR_YELLOW;    // Yellow (light discharge)
    if (current_a < 10) return 0xBDF7;          // White/Light gray (idle/slow charge)
    if (current_a < 30) return COLOR_GREEN;     // Green (normal charge)
    return COLOR_YELLOW;                        // Yellow (fast charge)
}

// ============================================================================
// BAR DRAWING FUNCTIONS
// ============================================================================

/**
 * Helper: Draw a segmented LED-style horizontal progress bar
 * @param x Starting X position
 * @param y Starting Y position
 * @param width Total bar width
 * @param height Bar height
 * @param filled_percent Percentage filled (0-100)
 * @param color Color of the filled portion
 */
static void draw_progress_bar(int x, int y, int width, int height, float filled_percent, uint16_t color) {
    // Clamp percentage
    if (filled_percent < 0) filled_percent = 0;
    if (filled_percent > 100) filled_percent = 100;
    
    // Configuration
    const int num_segments = 20;  // Number of LED segments
    const int gap = 2;            // Gap between segments
    
    // Calculate segment dimensions
    int total_gap_width = gap * (num_segments - 1);
    int available_width = width - total_gap_width;
    int segment_width = available_width / num_segments;
    
    if (segment_width < 2) {
        segment_width = 2;
        // Fallback: reduce segments if width is too small
    }
    
    // Draw background
    display_fill_rect(x, y, width, height, 0x0000); // Black background
    
    // Calculate how many segments should be filled
    int filled_segments = (int)((num_segments * filled_percent) / 100.0f);
    
    // Draw each segment
    for (int i = 0; i < num_segments; i++) {
        int seg_x = x + i * (segment_width + gap);
        
        if (i < filled_segments) {
            // Filled segment - full brightness
            display_fill_rect(seg_x, y, segment_width, height, color);
        } else {
            // Empty segment - very dark version of the color
            uint16_t dark_color = 0x2104; // Dark gray for all empty segments
            display_fill_rect(seg_x, y, segment_width, height, dark_color);
        }
    }
    
    // Draw outer border for the entire bar
    display_fill_rect(x - 1, y - 1, width + 2, 1, 0x630C);           // Top border (dim gray)
    display_fill_rect(x - 1, y + height, width + 2, 1, 0x630C);      // Bottom border
    display_fill_rect(x - 1, y - 1, 1, height + 2, 0x630C);          // Left border
    display_fill_rect(x + width, y - 1, 1, height + 2, 0x630C);      // Right border
}

/**
 * Draw MPPT Power bar (0-450W)
 */
void draw_mppt_power_bar(int x, int y, int width, int power_w) {
    // Clamp power to max
    if (power_w > 450) power_w = 450;
    if (power_w < 0) power_w = 0;

    float percent = (power_w / 450.0f) * 100.0f;
    uint16_t color = get_mppt_color(power_w);

    if (width < 10) width = 10;
    draw_progress_bar(x, y, width, 12, percent, color);
}

/**
 * Draw Battery Sense Temperature bar (-10°C to +50°C)
 */
void draw_battery_temp_bar(int x, int y, int width, float temp_c) {
    // Clamp temperature
    if (temp_c < -10) temp_c = -10;
    if (temp_c > 50) temp_c = 50;
    
    // Convert to 0-100 scale: -10°C = 0%, +50°C = 100%
    float percent = ((temp_c + 10) / 60.0f) * 100.0f;
    uint16_t color = get_battery_temp_color(temp_c);
    
    if (width < 10) width = 10;
    draw_progress_bar(x, y, width, 12, percent, color);
}

/**
 * Draw SmartShunt SOC bar (0-100%)
 */
void draw_smartshunt_soc_bar(int x, int y, int width, float soc_percent) {
    // Clamp SOC
    if (soc_percent < 0) soc_percent = 0;
    if (soc_percent > 100) soc_percent = 100;
    
    uint16_t color = get_soc_color(soc_percent);
    
    if (width < 10) width = 10;
    draw_progress_bar(x, y, width, 12, soc_percent, color);
}

/**
 * Draw SmartShunt Current bar (-100A to +50A)
 */
void draw_smartshunt_current_bar(int x, int y, int width, float current_a) {
    // Clamp current
    if (current_a < -100) current_a = -100;
    if (current_a > 50) current_a = 50;
    
    // Convert to 0-100 scale: -100A = 0%, +50A = 100%
    float percent = ((current_a + 100) / 150.0f) * 100.0f;
    uint16_t color = get_current_color(current_a);
    
    if (width < 10) width = 10;
    draw_progress_bar(x, y, width, 12, percent, color);
}
