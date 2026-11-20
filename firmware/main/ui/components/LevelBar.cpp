#include "LevelBar.hpp"

#include <cinttypes>

#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"

static constexpr int32_t TRACK_HEIGHT = 36;
static constexpr int32_t MARK_WIDTH = 3;
static constexpr int32_t FULL_SCALE = 4095;

/**
 * Parts of the bar, kept on the panel for the observers.
 */
struct LevelBarParts {
    UiState::Latency* latency;
    lv_obj_t* band;      // Range over the last 100 ms
    lv_obj_t* fill;      // Live level
    lv_obj_t* dark;      // Learned dark level
    lv_obj_t* threshold; // Detection threshold
    lv_obj_t* bright;    // Learned bright level
    lv_obj_t* value;     // "Light 62 %"
    lv_obj_t* detail;    // Contrast and the marks' legend
};

/**
 * Returns a level as a percentage of the track's width.
 *
 * @param level Brightness 0-4095
 * @return Percentage 0-100
 */
static int32_t toPercent(int32_t level) {
    int32_t clamped = level < 0 ? 0 : (level > FULL_SCALE ? FULL_SCALE : level);

    return clamped * 100 / FULL_SCALE;
}

static void onLevelChanged(lv_observer_t* observer, lv_subject_t* subject) {
    LevelBarParts* parts = static_cast<LevelBarParts*>(lv_observer_get_user_data(observer));
    UiState::Latency& latency = *parts->latency;
    int32_t level = lv_subject_get_int(&latency.level);
    int32_t lowest = toPercent(lv_subject_get_int(&latency.levelMin));
    int32_t highest = toPercent(lv_subject_get_int(&latency.levelMax));

    lv_obj_set_width(parts->fill, lv_pct(toPercent(level)));
    lv_obj_set_x(parts->band, lv_pct(lowest));
    lv_obj_set_width(parts->band, lv_pct(highest > lowest ? highest - lowest : 0));
    lv_label_set_text_fmt(parts->value, "%" PRId32 " %%", toPercent(level));
}

static void onLevelsChanged(lv_observer_t* observer, lv_subject_t* subject) {
    LevelBarParts* parts = static_cast<LevelBarParts*>(lv_observer_get_user_data(observer));
    UiState::Latency& latency = *parts->latency;
    bool hasLevels = lv_subject_get_int(&latency.hasLevels) != 0;
    lv_obj_t* marks[] = {parts->dark, parts->threshold, parts->bright};
    lv_subject_t* subjects[] = {&latency.darkLevel, &latency.threshold, &latency.brightLevel};

    for (size_t index = 0; index < 3; index++) {
        if (hasLevels) {
            lv_obj_set_x(marks[index], lv_pct(toPercent(lv_subject_get_int(subjects[index]))));
            lv_obj_remove_flag(marks[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(marks[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!hasLevels) {
        lv_label_set_text(parts->detail, "Dark, threshold and bright marks appear once a run has learned them");

        return;
    }

    int32_t contrast = lv_subject_get_int(&latency.contrastPerMille);
    lv_label_set_text_fmt(parts->detail, "Contrast %" PRId32 ".%" PRId32 " %%. Marks: dark in grey, threshold in amber, bright in white", contrast / 10, contrast % 10);
}

static void onDeleted(lv_event_t* event) {
    delete static_cast<LevelBarParts*>(lv_event_get_user_data(event));
}

/**
 * Adds a full-height mark to the track.
 *
 * @param track The track
 * @param color Theme colour token
 * @return The mark, hidden
 */
static lv_obj_t* addMark(lv_obj_t* track, uint32_t color) {
    lv_obj_t* mark = lv_obj_create(track);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, MARK_WIDTH, lv_pct(100));
    lv_obj_set_style_bg_color(mark, Theme::color(color), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);

    return mark;
}

lv_obj_t* createLevelBar(lv_obj_t* parent, UiState::Latency& latency) {
    LevelBarParts* parts = new LevelBarParts {};
    parts->latency = &latency;

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &Theme::surfaceStyle, 0);
    lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_S, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, onDeleted, LV_EVENT_DELETE, parts);

    // Title and the live value
    lv_obj_t* header = lv_obj_create(panel);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Light on the sensor");
    lv_obj_add_style(title, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(title, Theme::FONT_TITLE_SMALL, 0);

    parts->value = lv_label_create(header);
    lv_obj_add_style(parts->value, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(parts->value, Theme::FONT_NUMBER_SMALL, 0);

    // Track: band, fill and marks, positioned in percent of its width
    lv_obj_t* track = lv_obj_create(panel);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, lv_pct(100), TRACK_HEIGHT);
    lv_obj_set_style_bg_color(track, Theme::color(Theme::SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, Theme::RADIUS / 2, 0);
    lv_obj_set_style_clip_corner(track, true, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);

    parts->band = lv_obj_create(track);
    lv_obj_remove_style_all(parts->band);
    lv_obj_set_size(parts->band, 0, lv_pct(100));
    lv_obj_set_style_bg_color(parts->band, Theme::color(Theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(parts->band, LV_OPA_30, 0);
    lv_obj_remove_flag(parts->band, LV_OBJ_FLAG_CLICKABLE);

    parts->fill = lv_obj_create(track);
    lv_obj_remove_style_all(parts->fill);
    lv_obj_set_size(parts->fill, 0, lv_pct(100));
    lv_obj_set_style_bg_color(parts->fill, Theme::color(Theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(parts->fill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(parts->fill, LV_OBJ_FLAG_CLICKABLE);

    parts->dark = addMark(track, Theme::TEXT_MUTED);
    parts->threshold = addMark(track, Theme::WARNING);
    parts->bright = addMark(track, Theme::TEXT);

    // Contrast and what the marks are
    parts->detail = lv_label_create(panel);
    lv_obj_set_width(parts->detail, lv_pct(100));
    lv_label_set_long_mode(parts->detail, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(parts->detail, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(parts->detail, Theme::FONT_BODY_SMALL, 0);

    Bindings::observeAll(panel, onLevelChanged, parts, {&latency.level, &latency.levelMin, &latency.levelMax});
    Bindings::observeAll(panel, onLevelsChanged, parts, {&latency.hasLevels, &latency.darkLevel, &latency.threshold, &latency.brightLevel, &latency.contrastPerMille});

    return panel;
}
