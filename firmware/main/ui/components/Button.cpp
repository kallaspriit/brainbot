#include "Button.hpp"

#include "ui/Theme.hpp"

// Outline width, and the tint an outline button gets while pressed
static constexpr int32_t OUTLINE_WIDTH = 2;
static constexpr lv_opa_t OUTLINE_PRESSED_OPACITY = LV_OPA_30;

/**
 * Returns the main colour for a button variant: the fill, or the outline and text for an outline variant.
 *
 * @param variant The variant
 * @return Colour token
 */
static uint32_t getMainColor(ButtonVariant variant) {
    switch (variant) {
        case ButtonVariant::PRIMARY:
        case ButtonVariant::PRIMARY_OUTLINE:
            return Theme::ACCENT;

        case ButtonVariant::DANGER:
        case ButtonVariant::DANGER_OUTLINE:
            return Theme::DANGER;

        case ButtonVariant::SECONDARY:
        case ButtonVariant::SECONDARY_OUTLINE:
        case ButtonVariant::GHOST:
            return Theme::SURFACE_RAISED;
    }

    return Theme::SURFACE_RAISED;
}

lv_obj_t* createButton(lv_obj_t* parent, const ButtonOptions& options) {
    lv_obj_t* button = lv_button_create(parent);

    // Shape; colours come from the variant, set once the labels exist. A compact button keeps the 64 px touch target by
    // extending its click area beyond what it draws (UX rule: big touch targets)
    int32_t height = options.height > 0 ? options.height : Theme::BUTTON_HEIGHT;

    if (options.isCompact) {
        height = Theme::BUTTON_HEIGHT_COMPACT;
        lv_obj_set_ext_click_area(button, (Theme::TOUCH_TARGET_MIN - Theme::BUTTON_HEIGHT_COMPACT) / 2);
    }

    lv_obj_set_height(button, height);
    lv_obj_set_style_min_width(button, options.isCompact ? Theme::BUTTON_HEIGHT_COMPACT : Theme::TOUCH_TARGET_MIN, 0);
    lv_obj_set_style_radius(button, Theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_hor(button, options.isCompact ? Theme::SPACE_M : Theme::SPACE_L, 0);
    lv_obj_add_style(button, &Theme::disabledStyle, LV_STATE_DISABLED);

    // Icon and text side by side, centred
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button, Theme::SPACE_S, 0);

    if (options.icon != nullptr) {
        lv_obj_t* iconLabel = lv_label_create(button);
        lv_label_set_text(iconLabel, options.icon);
        lv_obj_set_style_text_font(iconLabel, options.isCompact ? Theme::FONT_ICON_SMALL : Theme::FONT_ICON, 0);
    }

    if (options.text != nullptr && options.text[0] != '\0') {
        lv_obj_t* textLabel = lv_label_create(button);
        lv_label_set_text(textLabel, options.text);
        lv_obj_set_style_text_font(textLabel, options.isCompact ? Theme::FONT_TITLE_SMALL : Theme::FONT_TITLE, 0);
    }

    setButtonVariant(button, options.variant);

    if (options.onClick != nullptr) {
        lv_obj_add_event_cb(button, options.onClick, LV_EVENT_CLICKED, options.userData);
    }

    return button;
}

void setButtonVariant(lv_obj_t* button, ButtonVariant variant) {
    // Quiet variants: transparent with the normal text colour, a raised surface while pressed; the outline one keeps the
    // secondary border so it still reads as a button
    if (variant == ButtonVariant::SECONDARY_OUTLINE || variant == ButtonVariant::GHOST) {
        bool hasBorder = variant == ButtonVariant::SECONDARY_OUTLINE;
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, hasBorder ? OUTLINE_WIDTH : 0, 0);
        lv_obj_set_style_border_color(button, Theme::color(Theme::BORDER), 0);
        lv_obj_set_style_border_color(button, Theme::color(Theme::BORDER), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(button, Theme::color(Theme::TEXT), 0);

        return;
    }

    lv_color_t mainColor = Theme::color(getMainColor(variant));
    bool isOutline = variant == ButtonVariant::DANGER_OUTLINE || variant == ButtonVariant::PRIMARY_OUTLINE;
    lv_color_t textColor = isOutline ? mainColor : Theme::color(Theme::TEXT);

    // Filled variants: solid background, darker when pressed. Outline: transparent, tinted when pressed
    lv_obj_set_style_bg_color(button, mainColor, 0);

    if (isOutline) {
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(button, OUTLINE_PRESSED_OPACITY, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, mainColor, LV_STATE_PRESSED);
    } else {
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_darken(mainColor, LV_OPA_20), LV_STATE_PRESSED);
    }

    // Border: a subtle edge on secondary buttons. Filled and outline variants share the outline width, filled ones in
    // their own colour, so switching between them doesn't move the content
    if (variant == ButtonVariant::SECONDARY) {
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, Theme::color(Theme::BORDER), 0);
        lv_obj_set_style_border_color(button, Theme::color(Theme::BORDER), LV_STATE_PRESSED);
    } else {
        lv_obj_set_style_border_width(button, OUTLINE_WIDTH, 0);
        lv_obj_set_style_border_color(button, mainColor, 0);
        lv_obj_set_style_border_color(button, isOutline ? mainColor : lv_color_darken(mainColor, LV_OPA_20), LV_STATE_PRESSED);
    }

    // Icon and text; labels inherit the text colour from the button
    lv_obj_set_style_text_color(button, textColor, 0);
}

void setButtonEnabled(lv_obj_t* button, bool isEnabled) {
    if (isEnabled) {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}
