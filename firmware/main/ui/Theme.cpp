#include "Theme.hpp"

namespace Theme {
lv_style_t screenStyle;
lv_style_t surfaceStyle;
lv_style_t pressedStyle;
lv_style_t disabledStyle;
lv_style_t textStyle;
lv_style_t mutedTextStyle;
lv_style_t scrollbarStyle;

void init(lv_display_t* display) {
    // LVGL's default theme in dark mode gives sensible widget defaults; our styles go on top
    lv_theme_t* theme = lv_theme_default_init(display, color(ACCENT), color(DANGER), true, FONT_BODY);
    lv_display_set_theme(display, theme);

    // Screen background
    lv_style_init(&screenStyle);
    lv_style_set_bg_color(&screenStyle, color(BACKGROUND));
    lv_style_set_bg_opa(&screenStyle, LV_OPA_COVER);
    lv_style_set_text_color(&screenStyle, color(TEXT));
    lv_style_set_text_font(&screenStyle, FONT_BODY);
    lv_style_set_pad_all(&screenStyle, 0);
    lv_style_set_border_width(&screenStyle, 0);

    // Cards and panels
    lv_style_init(&surfaceStyle);
    lv_style_set_bg_color(&surfaceStyle, color(SURFACE));
    lv_style_set_bg_opa(&surfaceStyle, LV_OPA_COVER);
    lv_style_set_border_color(&surfaceStyle, color(BORDER));
    lv_style_set_border_width(&surfaceStyle, 1);
    lv_style_set_radius(&surfaceStyle, RADIUS);
    lv_style_set_pad_all(&surfaceStyle, SPACE_L);
    lv_style_set_shadow_width(&surfaceStyle, 0);

    // Pressed: lift the surface and outline it in the accent colour
    lv_style_init(&pressedStyle);
    lv_style_set_bg_color(&pressedStyle, color(SURFACE_RAISED));
    lv_style_set_border_color(&pressedStyle, color(ACCENT));

    // Disabled: faded, the shape stays so the layout doesn't shift
    lv_style_init(&disabledStyle);
    lv_style_set_opa(&disabledStyle, DISABLED_OPACITY);

    // Text
    lv_style_init(&textStyle);
    lv_style_set_text_color(&textStyle, color(TEXT));

    lv_style_init(&mutedTextStyle);
    lv_style_set_text_color(&mutedTextStyle, color(TEXT_MUTED));

    // Scrollbar: a slim rounded bar in the muted text colour, so a list that scrolls looks like it does
    lv_style_init(&scrollbarStyle);
    lv_style_set_width(&scrollbarStyle, SCROLLBAR_WIDTH);
    lv_style_set_bg_color(&scrollbarStyle, color(TEXT_MUTED));
    lv_style_set_bg_opa(&scrollbarStyle, LV_OPA_60);
    lv_style_set_radius(&scrollbarStyle, LV_RADIUS_CIRCLE);
    lv_style_set_pad_right(&scrollbarStyle, 0);
}
} // namespace Theme
