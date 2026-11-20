#include "SegmentedToggle.hpp"

#include "ui/Theme.hpp"

// Smaller than a motion button: a mode switch sits in a control's header; the touch area is extended to 64 px
static constexpr int32_t TOGGLE_HEIGHT = 48;
static constexpr int32_t TOUCH_EXTENSION = (Theme::BUTTON_HEIGHT - TOGGLE_HEIGHT) / 2;
static constexpr lv_opa_t SELECTED_OPA = LV_OPA_20; // The picked segment's accent tint: unlike a solid button

/**
 * What a toggle needs after creation, owned by the toggle and freed with it.
 */
struct SegmentedToggle {
    SegmentedToggleOptions options; // Copy of the creation options
    lv_obj_t* segments[2];          // Option segments
};

/**
 * Picks the tapped option.
 *
 * @param event The click event; its user data is the toggle state, the segment's user data the option index
 */
static void onOptionClicked(lv_event_t* event) {
    SegmentedToggle* toggle = static_cast<SegmentedToggle*>(lv_event_get_user_data(event));
    int32_t index = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event))));

    if (toggle->options.onChange != nullptr) {
        toggle->options.onChange(index, toggle->options.userData);
    }

    lv_subject_set_int(toggle->options.subject, index);
}

/**
 * Marks the picked option: an accent tint and accent text; the other is muted.
 *
 * @param observer The observer; its user data is the toggle state
 * @param subject The option subject
 */
static void onSubjectChanged(lv_observer_t* observer, lv_subject_t* subject) {
    SegmentedToggle* toggle = static_cast<SegmentedToggle*>(lv_observer_get_user_data(observer));
    int32_t index = lv_subject_get_int(subject);

    for (int32_t option = 0; option < 2; option++) {
        lv_obj_t* segment = toggle->segments[option];
        bool isPicked = option == index;
        lv_obj_set_style_bg_opa(segment, isPicked ? SELECTED_OPA : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(segment, Theme::color(isPicked ? Theme::ACCENT : Theme::TEXT_MUTED), 0);
    }
}

/**
 * Frees the toggle state when the toggle is deleted.
 *
 * @param event The delete event; its user data is the toggle state
 */
static void onDeleted(lv_event_t* event) {
    delete static_cast<SegmentedToggle*>(lv_event_get_user_data(event));
}

lv_obj_t* createSegmentedToggle(lv_obj_t* parent, const SegmentedToggleOptions& options) {
    SegmentedToggle* toggle = new SegmentedToggle {options, {nullptr, nullptr}};

    // One outlined frame; the segments share its inside and a divider between them
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_SIZE_CONTENT, TOGGLE_HEIGHT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_radius(container, Theme::RADIUS, 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_color(container, Theme::color(Theme::BORDER), 0);
    lv_obj_set_style_clip_corner(container, true, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(container, onDeleted, LV_EVENT_DELETE, toggle);

    const char* labels[2] = {options.first, options.second};

    for (int32_t option = 0; option < 2; option++) {
        lv_obj_t* segment = lv_obj_create(container);
        lv_obj_remove_style_all(segment);
        lv_obj_set_height(segment, lv_pct(100));
        lv_obj_set_width(segment, LV_SIZE_CONTENT);

        // A stretched toggle shares its width; one sized to content would squeeze growing segments to nothing
        if (options.isStretched) {
            lv_obj_set_flex_grow(segment, 1);
        }

        lv_obj_set_style_pad_hor(segment, Theme::SPACE_M, 0);
        lv_obj_set_style_bg_color(segment, Theme::color(Theme::ACCENT), 0);
        lv_obj_set_style_bg_color(segment, Theme::color(Theme::SURFACE_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_text_font(segment, Theme::FONT_TITLE_SMALL, 0);
        lv_obj_set_ext_click_area(segment, TOUCH_EXTENSION);
        lv_obj_remove_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(segment, onOptionClicked, LV_EVENT_CLICKED, toggle);
        lv_obj_set_user_data(segment, reinterpret_cast<void*>(static_cast<intptr_t>(option)));

        // The shared middle line
        if (option == 1) {
            lv_obj_set_style_border_side(segment, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(segment, 1, 0);
            lv_obj_set_style_border_color(segment, Theme::color(Theme::BORDER), 0);
        }

        lv_obj_t* label = lv_label_create(segment);
        lv_label_set_text(label, labels[option]);
        lv_obj_center(label);
        toggle->segments[option] = segment;
    }

    // Follow the subject; LVGL removes the observer with the container, before the state is freed
    lv_subject_add_observer_obj(options.subject, onSubjectChanged, container, toggle);

    return container;
}
