#include "NumberControl.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/NumberPad.hpp"

/**
 * What a control needs after creation, owned by the control and freed with it.
 */
struct NumberControl {
    NumberControlOptions options; // Copy of the creation options
    lv_obj_t* valueLabel;         // Value in the header
    lv_obj_t* slider;             // Coarse adjustment
};

/**
 * Clamps and applies a new value: reports it and shows it straight away.
 *
 * @param control The control
 * @param value Requested value
 */
static void applyValue(NumberControl* control, int32_t value) {
    int32_t clamped = std::clamp(value, control->options.min, control->options.max);

    if (control->options.onChange != nullptr) {
        control->options.onChange(clamped, control->options.userData);
    }

    lv_subject_set_int(control->options.subject, clamped);
}

/**
 * Applies a value typed on the number pad.
 *
 * @param value The confirmed value
 * @param userData The control
 */
static void onPadConfirmed(int32_t value, void* userData) {
    applyValue(static_cast<NumberControl*>(userData), value);
}

/**
 * Opens the number pad when the header is tapped.
 *
 * @param event The click event; its user data is the control
 */
static void onHeaderClicked(lv_event_t* event) {
    NumberControl* control = static_cast<NumberControl*>(lv_event_get_user_data(event));
    const NumberControlOptions& options = control->options;

    NumberPadOptions padOptions;
    padOptions.title = options.label;
    padOptions.unit = options.unit;
    padOptions.value = lv_subject_get_int(options.subject);
    padOptions.min = options.min;
    padOptions.max = options.max;
    padOptions.decimals = options.decimals;
    padOptions.onConfirm = onPadConfirmed;
    padOptions.userData = control;
    openNumberPad(options.overlayLayer, padOptions);
}

/**
 * Follows a new largest value: range, slider and the current value clamped to it.
 *
 * @param observer The observer; its user data is the control
 * @param subject The max subject
 */
static void onMaxChanged(lv_observer_t* observer, lv_subject_t* subject) {
    NumberControl* control = static_cast<NumberControl*>(lv_observer_get_user_data(observer));
    int32_t max = lv_subject_get_int(subject);

    // Not known yet (0): keep the creation range
    if (max <= 0) {
        return;
    }

    control->options.max = max;

    if (control->options.isSymmetric) {
        control->options.min = -max;
    }

    if (control->slider != nullptr) {
        lv_slider_set_range(control->slider, control->options.min, control->options.max);
        lv_slider_set_value(control->slider, lv_subject_get_int(control->options.subject), LV_ANIM_OFF);
    }
}

/**
 * Applies the slider's value while it is dragged.
 *
 * @param event The value-changed event; its user data is the control
 */
static void onSliderChanged(lv_event_t* event) {
    NumberControl* control = static_cast<NumberControl*>(lv_event_get_user_data(event));

    applyValue(control, lv_slider_get_value(control->slider));
}

/**
 * Adds the tapped button's step to the current value.
 *
 * @param event The click event; its user data is the control, the button's user data the step
 */
static void onStepClicked(lv_event_t* event) {
    NumberControl* control = static_cast<NumberControl*>(lv_event_get_user_data(event));
    lv_obj_t* button = lv_event_get_current_target_obj(event);
    int32_t step = static_cast<int32_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));

    applyValue(control, lv_subject_get_int(control->options.subject) + step);
}

/**
 * Shows the subject's value in the header and on the slider.
 *
 * @param observer The observer; its user data is the control
 * @param subject The value subject
 */
static void onValueChanged(lv_observer_t* observer, lv_subject_t* subject) {
    NumberControl* control = static_cast<NumberControl*>(lv_observer_get_user_data(observer));
    int32_t value = lv_subject_get_int(subject);

    char text[24];
    Bindings::formatFixedPoint(text, sizeof(text), value, control->options.decimals, "");
    lv_label_set_text(control->valueLabel, text);

    // Leave the slider alone while it is dragged, so a late update can't pull it back
    if (control->slider != nullptr && !lv_obj_has_state(control->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(control->slider, value, LV_ANIM_OFF);
    }
}

/**
 * Formats a step for its button: signed, with only the decimals it needs ("+0.25", "-10").
 *
 * @param buffer Output buffer
 * @param bufferSize Size of the buffer
 * @param step Step in the subject's units
 * @param decimals Fixed-point decimals of the subject
 */
static void formatStep(char* buffer, size_t bufferSize, int32_t step, uint8_t decimals) {
    char magnitude[16];
    Bindings::formatFixedPoint(magnitude, sizeof(magnitude), std::abs(step), decimals, "");

    // Drop trailing zeros of the fraction, then the point if nothing is left after it
    if (std::strchr(magnitude, '.') != nullptr) {
        size_t length = std::strlen(magnitude);

        while (length > 0 && magnitude[length - 1] == '0') {
            magnitude[--length] = '\0';
        }

        if (length > 0 && magnitude[length - 1] == '.') {
            magnitude[length - 1] = '\0';
        }
    }

    snprintf(buffer, bufferSize, "%s%s", step < 0 ? "-" : "+", magnitude);
}

/**
 * Frees the control's state when it is deleted.
 *
 * @param event The delete event; its user data is the control
 */
static void onDeleted(lv_event_t* event) {
    delete static_cast<NumberControl*>(lv_event_get_user_data(event));
}

lv_obj_t* createNumberControl(lv_obj_t* parent, const NumberControlOptions& options) {
    NumberControl* control = new NumberControl {options, nullptr, nullptr};

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, Theme::SPACE_S, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container, onDeleted, LV_EVENT_DELETE, control);

    // Header: name on the left, value and unit on the right
    lv_obj_t* header = lv_obj_create(container);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(header, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* nameLabel = lv_label_create(header);
    lv_label_set_text(nameLabel, options.label);
    lv_obj_set_flex_grow(nameLabel, 1);
    lv_obj_add_style(nameLabel, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(nameLabel, Theme::FONT_TITLE, 0);

    // Tapping the value (anywhere on the header) types a number, when there is an overlay for the pad
    if (options.overlayLayer != nullptr) {
        lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(header, onHeaderClicked, LV_EVENT_CLICKED, control);
    }

    control->valueLabel = lv_label_create(header);
    lv_obj_set_style_text_font(control->valueLabel, Theme::FONT_NUMBER, 0);
    lv_obj_set_style_text_color(control->valueLabel, Theme::color(Theme::TEXT), 0);

    // Underlined when tappable, so the value reads as something to press
    if (options.overlayLayer != nullptr) {
        lv_obj_set_style_text_decor(control->valueLabel, LV_TEXT_DECOR_UNDERLINE, 0);
    }

    lv_obj_t* unitLabel = lv_label_create(header);
    lv_label_set_text(unitLabel, options.unit);
    lv_obj_add_style(unitLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(unitLabel, Theme::FONT_BODY, 0);
    lv_obj_set_style_pad_bottom(unitLabel, Theme::SPACE_XS, 0);

    // Slider, inset so its knob stays inside the column
    if (options.hasSlider) {
        control->slider = lv_slider_create(container);
        lv_obj_set_width(control->slider, lv_pct(100));
        lv_obj_set_height(control->slider, Theme::SPACE_M);
        lv_obj_set_style_margin_hor(control->slider, Theme::SPACE_M, 0);
        lv_obj_set_style_margin_ver(control->slider, Theme::SPACE_S, 0);
        lv_slider_set_range(control->slider, options.min, options.max);
        lv_obj_add_event_cb(control->slider, onSliderChanged, LV_EVENT_VALUE_CHANGED, control);
    }

    // Step buttons, sharing the width
    lv_obj_t* steps = lv_obj_create(container);
    lv_obj_remove_style_all(steps);
    lv_obj_set_size(steps, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(steps, Theme::SPACE_S, 0);
    lv_obj_remove_flag(steps, LV_OBJ_FLAG_CLICKABLE);

    for (size_t index = 0; index < options.stepCount; index++) {
        int32_t step = options.steps[index];
        char text[12];
        formatStep(text, sizeof(text), step, options.decimals);

        ButtonOptions buttonOptions;
        buttonOptions.text = text;
        buttonOptions.onClick = onStepClicked;
        buttonOptions.userData = control;
        lv_obj_t* button = createButton(steps, buttonOptions);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_pad_hor(button, 0, 0);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(step)));
    }

    // Follow the subject and the limit; LVGL removes the observers with the container, before the state is freed
    lv_subject_add_observer_obj(options.subject, onValueChanged, container, control);

    if (options.maxSubject != nullptr) {
        lv_subject_add_observer_obj(options.maxSubject, onMaxChanged, container, control);
    }

    return container;
}

lv_obj_t* getNumberControlHeader(lv_obj_t* control) {
    return lv_obj_get_child(control, 0);
}
