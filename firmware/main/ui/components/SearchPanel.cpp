#include "SearchPanel.hpp"

#include <cinttypes>
#include <cstdint>

#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"

static constexpr int32_t SPINNER_SIZE = 72;

// What to check when no motor answers; static, so it outlives the labels
static const char* const SEARCH_CHECKLIST[] = {
    "Both cables plugged in: power and CAN.",
    "Motor powered: green RUN LED steady, yellow COM LED blinking slowly. All dark means no power; fast blinking means a warning or fault.",
    "At the console end: CAN H, CAN L and CAN ground on the board's H, L and GND pins. Never the board's 5 V.",
    "The CAN icon at the top right turns amber or red when the controller sees bus errors.",
    "LEDs fine and still not found: the drive may be set to another communication mode, which needs NimStudio over RS485.",
};

static void onSearchStatusChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState& state = *static_cast<UiState*>(lv_observer_get_user_data(observer));
    lv_obj_t* label = lv_observer_get_target_obj(observer);

    if (!lv_subject_get_int(&state.system.isCanStarted)) {
        lv_label_set_text(label, "The CAN controller didn't start; restart the console");

        return;
    }

    // On an empty bus a sweep of every node ID and bitrate takes a fraction of a second, so count sweeps, not steps
    int32_t roundCount = lv_subject_get_int(&state.motor.searchRoundCount);

    if (roundCount == 0) {
        lv_label_set_text(label, "No answer yet at node 1, 1 Mbit/s, the NiMotion and PTU default");
    } else {
        lv_label_set_text_fmt(label, "No answer at any node ID or bitrate (searched %" PRId32 " times). Still searching", roundCount);
    }
}

lv_obj_t* createSearchPanel(lv_obj_t* parent, UiState& state) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_M, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_bind_flag_if_eq(panel, &state.motor.isConnected, LV_OBJ_FLAG_HIDDEN, 1);

    // Spinner beside the title and where the search is
    lv_obj_t* header = lv_obj_create(panel);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, Theme::SPACE_L, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* spinner = lv_spinner_create(header);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, Theme::SPACE_S, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, Theme::SPACE_S, LV_PART_INDICATOR);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* titles = lv_obj_create(header);
    lv_obj_remove_style_all(titles);
    lv_obj_set_height(titles, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(titles, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(titles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(titles, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(titles);
    lv_label_set_text(title, "Looking for a motor");
    lv_obj_add_style(title, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(title, Theme::FONT_TITLE_LARGE, 0);

    lv_obj_t* status = lv_label_create(titles);
    lv_obj_add_style(status, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(status, Theme::FONT_BODY, 0);
    Bindings::observeAll(status, onSearchStatusChanged, &state, {&state.system.isCanStarted, &state.motor.searchRoundCount});

    // What to check
    for (const char* point : SEARCH_CHECKLIST) {
        lv_obj_t* label = lv_label_create(panel);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(label, "•  %s", point);
        lv_obj_add_style(label, &Theme::textStyle, 0);
        lv_obj_set_style_text_font(label, Theme::FONT_BODY, 0);
    }

    return panel;
}
