#include "InfoPanel.hpp"

#include "ui/Theme.hpp"

lv_obj_t* createInfoPanel(lv_obj_t* parent, const char* title) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_XS, 0);
    lv_obj_set_style_text_font(panel, Theme::FONT_BODY_SMALL, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* titleLabel = lv_label_create(panel);
    lv_label_set_text(titleLabel, title);
    lv_obj_add_style(titleLabel, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(titleLabel, Theme::FONT_TITLE_SMALL, 0);
    lv_obj_set_style_pad_bottom(titleLabel, Theme::SPACE_XS, 0);

    return panel;
}

lv_obj_t* createDivider(lv_obj_t* parent) {
    lv_obj_t* divider = lv_obj_create(parent);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 1, lv_pct(100));
    lv_obj_set_style_bg_color(divider, Theme::color(Theme::BORDER), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    return divider;
}

lv_obj_t* addInfoRow(lv_obj_t* panel, const char* name, int32_t nameWidth) {
    lv_obj_t* row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* nameLabel = lv_label_create(row);
    lv_label_set_text(nameLabel, name);
    lv_obj_set_width(nameLabel, nameWidth);
    lv_obj_add_style(nameLabel, &Theme::mutedTextStyle, 0);

    lv_obj_t* valueLabel = lv_label_create(row);
    lv_obj_set_flex_grow(valueLabel, 1);
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(valueLabel, &Theme::textStyle, 0);

    return valueLabel;
}
