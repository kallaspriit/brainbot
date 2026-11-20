#include "HomeCard.hpp"

#include "ui/Theme.hpp"

lv_obj_t* createHomeCard(lv_obj_t* parent, const HomeCardOptions& options) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &Theme::surfaceStyle, 0);
    lv_obj_add_style(card, &Theme::pressedStyle, LV_STATE_PRESSED);
    lv_obj_add_style(card, &Theme::disabledStyle, LV_STATE_DISABLED);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Icon at the top, title and status at the bottom
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* iconLabel = lv_label_create(card);
    lv_label_set_text(iconLabel, options.icon);
    lv_obj_set_style_text_font(iconLabel, Theme::FONT_ICON_LARGE, 0);
    lv_obj_set_style_text_color(iconLabel, Theme::color(Theme::ACCENT), 0);

    lv_obj_t* textGroup = lv_obj_create(card);
    lv_obj_remove_style_all(textGroup);
    lv_obj_set_size(textGroup, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(textGroup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(textGroup, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(textGroup, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* titleLabel = lv_label_create(textGroup);
    lv_label_set_text(titleLabel, options.title);
    lv_obj_set_style_text_font(titleLabel, Theme::FONT_TITLE, 0);
    lv_obj_add_style(titleLabel, &Theme::textStyle, 0);

    lv_obj_t* statusLabel = lv_label_create(textGroup);
    lv_label_set_text(statusLabel, options.status);
    // One line, cut with an ellipsis, so every card's title sits at the same height
    lv_obj_set_size(statusLabel, lv_pct(100), lv_font_get_line_height(Theme::FONT_BODY_SMALL));
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(statusLabel, Theme::FONT_BODY_SMALL, 0);
    lv_obj_add_style(statusLabel, &Theme::mutedTextStyle, 0);

    // Tappable unless disabled
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    if (!options.isEnabled) {
        lv_obj_add_state(card, LV_STATE_DISABLED);
    }

    if (options.onClick != nullptr) {
        lv_obj_add_event_cb(card, options.onClick, LV_EVENT_CLICKED, options.userData);
    }

    return card;
}
