#include "TabStrip.hpp"

#include "ui/Theme.hpp"

namespace {
struct TabStripState {
    TabStripCallback onChange;
    void* userData;
};
} // namespace

static constexpr int32_t UNDERLINE_WIDTH = 3;

/**
 * Shows one tab as picked and the others as not.
 *
 * @param strip The strip
 * @param selected Picked tab
 */
static void showSelected(lv_obj_t* strip, uint32_t selected) {
    uint32_t count = lv_obj_get_child_count(strip);

    for (uint32_t index = 0; index < count; index++) {
        lv_obj_t* tab = lv_obj_get_child(strip, static_cast<int32_t>(index));
        lv_obj_t* label = lv_obj_get_child(tab, 0);
        bool isSelected = index == selected;

        lv_obj_set_style_text_color(label, Theme::color(isSelected ? Theme::ACCENT : Theme::TEXT_MUTED), 0);
        lv_obj_set_style_border_opa(tab, isSelected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

static void onTabClicked(lv_event_t* event) {
    lv_obj_t* tab = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    lv_obj_t* strip = lv_obj_get_parent(tab);
    TabStripState* state = static_cast<TabStripState*>(lv_obj_get_user_data(strip));
    int32_t index = lv_obj_get_index(tab);

    showSelected(strip, static_cast<uint32_t>(index));

    if (state->onChange != nullptr) {
        state->onChange(index, state->userData);
    }
}

static void onStripDeleted(lv_event_t* event) {
    lv_obj_t* strip = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    delete static_cast<TabStripState*>(lv_obj_get_user_data(strip));
}

lv_obj_t* createTabStrip(lv_obj_t* parent, const TabStripOptions& options) {
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, lv_pct(100), Theme::TOUCH_TARGET_MIN);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(strip, Theme::SPACE_L, 0);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_border_color(strip, Theme::color(Theme::BORDER), 0);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(strip, new TabStripState {options.onChange, options.userData});
    lv_obj_add_event_cb(strip, onStripDeleted, LV_EVENT_DELETE, nullptr);

    // Each tab: its label, padded to a full touch target, with an accent underline when picked
    for (size_t index = 0; index < options.count; index++) {
        lv_obj_t* tab = lv_obj_create(strip);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, LV_SIZE_CONTENT, lv_pct(100));
        lv_obj_set_style_pad_hor(tab, Theme::SPACE_S, 0);
        lv_obj_set_style_border_side(tab, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(tab, UNDERLINE_WIDTH, 0);
        lv_obj_set_style_border_color(tab, Theme::color(Theme::ACCENT), 0);
        lv_obj_add_style(tab, &Theme::pressedStyle, LV_STATE_PRESSED);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tab, onTabClicked, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* label = lv_label_create(tab);
        lv_label_set_text(label, options.labels[index]);
        lv_obj_set_style_text_font(label, Theme::FONT_TITLE_SMALL, 0);
        lv_obj_center(label);
    }

    showSelected(strip, static_cast<uint32_t>(options.selected));

    return strip;
}
