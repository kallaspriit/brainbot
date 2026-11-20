#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/fonts/Fonts.hpp"

/**
 * Design tokens and shared styles (docs/UI design.md).
 *
 * Screens and components take colours, spacing, sizes and fonts only from here. Styles are created once by init().
 */
namespace Theme {
// Colours
constexpr uint32_t BACKGROUND = 0x0F1216;      // Screen
constexpr uint32_t SURFACE = 0x171B21;         // Cards, panels
constexpr uint32_t SURFACE_RAISED = 0x1F252D;  // Pressed, selected, dialogs
constexpr uint32_t BORDER = 0x2A313B;          // Dividers, outlines
constexpr uint32_t TEXT = 0xE8ECF1;            // Primary text
constexpr uint32_t TEXT_MUTED = 0x9AA4B2;      // Labels, secondary text
constexpr uint32_t ACCENT = 0x3B82F6;          // Selection, primary actions
constexpr uint32_t SUCCESS = 0x22C55E;         // Running, OK
constexpr uint32_t WARNING = 0xF59E0B;         // Warnings
constexpr uint32_t DANGER = 0xEF4444;          // STOP and faults only
constexpr uint32_t CHART_SECONDARY = 0xC084FC; // Lines on a chart's second scale (motor current)

// Spacing scale
constexpr int32_t SPACE_XS = 4;
constexpr int32_t SPACE_S = 8;
constexpr int32_t SPACE_M = 16;
constexpr int32_t SPACE_L = 24;
constexpr int32_t SPACE_XL = 32;

// Shape and sizes
constexpr int32_t RADIUS = 12;
constexpr int32_t TOUCH_TARGET_MIN = 64; // Smallest tappable size, bench use with gloves
constexpr int32_t TOP_BAR_HEIGHT = 72;
constexpr int32_t BUTTON_HEIGHT = 64;
constexpr int32_t BUTTON_HEIGHT_LARGE = 88;
constexpr int32_t BUTTON_HEIGHT_COMPACT = 44; // Looks smaller; the touch area is extended to TOUCH_TARGET_MIN
constexpr int32_t DISABLED_OPACITY = LV_OPA_40;
constexpr int32_t SCROLLBAR_WIDTH = 6; // Leave this much plus SPACE_S free at the right of a scrolling list

// Fonts
inline const lv_font_t* const FONT_BODY_SMALL = &inter_regular_16;
inline const lv_font_t* const FONT_BODY = &inter_regular_20;
inline const lv_font_t* const FONT_BODY_LARGE = &inter_regular_24;
inline const lv_font_t* const FONT_TITLE_SMALL = &inter_semibold_20;
inline const lv_font_t* const FONT_TITLE = &inter_semibold_24;
inline const lv_font_t* const FONT_TITLE_LARGE = &inter_semibold_32;
inline const lv_font_t* const FONT_CODE = &mono_medium_16; // Codes, raw values and times in lists, at body-small size
inline const lv_font_t* const FONT_NUMBER_SMALL = &mono_medium_24;
inline const lv_font_t* const FONT_NUMBER = &mono_medium_32;
inline const lv_font_t* const FONT_NUMBER_LARGE = &mono_medium_48;
inline const lv_font_t* const FONT_ICON_SMALL = &icons_24;
inline const lv_font_t* const FONT_ICON = &icons_32;
inline const lv_font_t* const FONT_ICON_LARGE = &icons_48;

/**
 * Sets LVGL's default theme to dark with the accent colour and creates the shared styles.
 *
 * Call once, holding the display lock, before creating any UI.
 *
 * @param display The display
 */
void init(lv_display_t* display);

/**
 * Returns a colour token as an LVGL colour.
 *
 * @param hex Colour token, e.g. Theme::ACCENT
 * @return The colour
 */
inline lv_color_t color(uint32_t hex) {
    return lv_color_hex(hex);
}

/**
 * Returns the colour token for an alarm's response code (bits 31-24 of a NiMotion alarm word): red for a fault (0-2),
 * amber for halt and warning (3-4), muted when ignored (5-6).
 *
 * @param responseCode Response code
 * @return Colour token
 */
inline uint32_t alarmColor(uint8_t responseCode) {
    if (responseCode <= 2) {
        return DANGER;
    }

    if (responseCode <= 4) {
        return WARNING;
    }

    return TEXT_MUTED;
}

// Shared styles, valid after init()
extern lv_style_t screenStyle;    // Screen background
extern lv_style_t surfaceStyle;   // Card or panel: surface, border, radius, padding
extern lv_style_t pressedStyle;   // Pressed state of tappable surfaces
extern lv_style_t disabledStyle;  // Disabled state of anything tappable
extern lv_style_t textStyle;      // Primary text
extern lv_style_t mutedTextStyle; // Secondary text
extern lv_style_t scrollbarStyle; // Visible scrollbar for lists that scroll; add to LV_PART_SCROLLBAR
} // namespace Theme
