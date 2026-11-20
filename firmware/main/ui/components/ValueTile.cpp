#include "ValueTile.hpp"

#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"

/**
 * Colours the value amber while the warning subject is non-zero.
 *
 * @param observer The observer; its target is the value label
 * @param subject The warning subject
 */
static void onWarningChanged(lv_observer_t* observer, lv_subject_t* subject) {
    bool hasWarning = lv_subject_get_int(subject) != 0;

    lv_obj_set_style_text_color(lv_observer_get_target_obj(observer), Theme::color(hasWarning ? Theme::WARNING : Theme::TEXT), 0);
}

lv_obj_t* createValueTile(lv_obj_t* parent, const ValueTileOptions& options) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_add_style(tile, &Theme::surfaceStyle, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* nameLabel = lv_label_create(tile);
    lv_label_set_text(nameLabel, options.label);
    lv_obj_add_style(nameLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(nameLabel, Theme::FONT_BODY_SMALL, 0);

    // Number and unit on one baseline
    lv_obj_t* valueRow = lv_obj_create(tile);
    lv_obj_remove_style_all(valueRow);
    lv_obj_set_size(valueRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(valueRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(valueRow, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(valueRow, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* valueLabel = lv_label_create(valueRow);
    lv_obj_set_style_text_font(valueLabel, options.isLarge ? Theme::FONT_NUMBER_LARGE : Theme::FONT_NUMBER, 0);
    lv_obj_set_style_text_color(valueLabel, Theme::color(Theme::TEXT), 0);
    Bindings::bindFixedPoint(valueLabel, options.subject, options.decimals, "");

    lv_obj_t* unitLabel = lv_label_create(valueRow);
    lv_label_set_text(unitLabel, options.unit);
    lv_obj_add_style(unitLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(unitLabel, Theme::FONT_BODY, 0);
    lv_obj_set_style_pad_bottom(unitLabel, Theme::SPACE_XS, 0);

    if (options.warningSubject != nullptr) {
        lv_subject_add_observer_obj(options.warningSubject, onWarningChanged, valueLabel, nullptr);
    }

    return tile;
}
