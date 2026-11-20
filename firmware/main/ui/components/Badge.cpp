#include "Badge.hpp"

#include "ui/Theme.hpp"

// Background tint and border strength of the badge colour
static constexpr lv_opa_t BACKGROUND_OPACITY = LV_OPA_20;
static constexpr lv_opa_t BORDER_OPACITY = LV_OPA_60;

lv_obj_t* createBadge(lv_obj_t* parent, bool isCompact) {
    lv_obj_t* badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badge, BACKGROUND_OPACITY, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_opa(badge, BORDER_OPACITY, 0);
    lv_obj_set_style_pad_hor(badge, isCompact ? Theme::SPACE_S : Theme::SPACE_M, 0);
    lv_obj_set_style_pad_ver(badge, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_column(badge, Theme::SPACE_XS, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    // Icon, then text; setBadge() fills them
    lv_obj_t* iconLabel = lv_label_create(badge);
    lv_obj_set_style_text_font(iconLabel, Theme::FONT_ICON_SMALL, 0);

    lv_obj_t* textLabel = lv_label_create(badge);
    lv_obj_set_style_text_font(textLabel, isCompact ? Theme::FONT_BODY_SMALL : Theme::FONT_TITLE_SMALL, 0);

    return badge;
}

void setBadge(lv_obj_t* badge, const char* icon, const char* text, uint32_t color) {
    lv_obj_t* iconLabel = lv_obj_get_child(badge, 0);
    lv_obj_t* textLabel = lv_obj_get_child(badge, 1);
    lv_color_t badgeColor = Theme::color(color);

    lv_obj_set_style_bg_color(badge, badgeColor, 0);
    lv_obj_set_style_border_color(badge, badgeColor, 0);
    lv_obj_set_style_text_color(iconLabel, badgeColor, 0);
    lv_obj_set_style_text_color(textLabel, badgeColor, 0);
    lv_label_set_text(textLabel, text);

    if (icon != nullptr) {
        lv_label_set_text(iconLabel, icon);
        lv_obj_remove_flag(iconLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(iconLabel, LV_OBJ_FLAG_HIDDEN);
    }
}
