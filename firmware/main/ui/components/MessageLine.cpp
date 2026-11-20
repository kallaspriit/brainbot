#include "MessageLine.hpp"

#include "ui/Theme.hpp"
#include "ui/fonts/Icons.hpp"

// How long a new message takes to fade in
static constexpr uint32_t FADE_IN_MS = 400;

// Children of the line, in creation order
static constexpr uint32_t CHILD_ICON = 0;
static constexpr uint32_t CHILD_TEXT = 1;

/**
 * Shows the latest message with the icon and colour of its level.
 *
 * @param observer The observer; its target is the line, its user data the UiState
 * @param subject The message sequence
 */
static void onMessageChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
    lv_obj_t* line = lv_observer_get_target_obj(observer);
    lv_obj_t* icon = lv_obj_get_child(line, CHILD_ICON);
    lv_obj_t* text = lv_obj_get_child(line, CHILD_TEXT);
    const char* message = lv_subject_get_string(&motor.message);
    MessageLevel level = static_cast<MessageLevel>(lv_subject_get_int(&motor.messageLevel));

    // Info reads as plain text with a muted icon; warning and error take their colour throughout
    const char* iconText = Icons::INFO;
    uint32_t iconColor = Theme::TEXT_MUTED;
    uint32_t textColor = Theme::TEXT;

    if (level == MessageLevel::WARNING) {
        iconText = Icons::WARNING;
        iconColor = Theme::WARNING;
        textColor = Theme::WARNING;
    } else if (level == MessageLevel::ERROR) {
        iconText = Icons::FAULT;
        iconColor = Theme::DANGER;
        textColor = Theme::DANGER;
    }

    lv_label_set_text(icon, iconText);
    lv_obj_set_style_text_color(icon, Theme::color(iconColor), 0);
    lv_label_set_text(text, message);
    lv_obj_set_style_text_color(text, Theme::color(textColor), 0);

    // No message, no icon
    if (message[0] == '\0') {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_fade_in(line, FADE_IN_MS, 0);
}

lv_obj_t* createMessageLine(lv_obj_t* parent, UiState& state) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(line, Theme::SPACE_S, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = lv_label_create(line);
    lv_obj_set_style_text_font(icon, Theme::FONT_ICON_SMALL, 0);

    lv_obj_t* text = lv_label_create(line);
    lv_obj_set_flex_grow(text, 1);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);

    lv_subject_add_observer_obj(&state.motor.messageSequence, onMessageChanged, line, &state);

    return line;
}
