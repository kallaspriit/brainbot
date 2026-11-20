#include "Bindings.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

namespace Bindings {
static constexpr uint8_t MAX_DECIMALS = 9;

/**
 * What a fixed-point binding needs to format its value, owned by the label's binding.
 */
struct FixedPointFormat {
    uint8_t decimals; // Decimal places
    const char* unit; // Unit text
};

/**
 * Updates the label when the subject changes.
 *
 * @param observer The binding
 * @param subject The subject that changed
 */
static void onFixedPointChanged(lv_observer_t* observer, lv_subject_t* subject) {
    const FixedPointFormat* format = static_cast<const FixedPointFormat*>(lv_observer_get_user_data(observer));
    char text[32];

    formatFixedPoint(text, sizeof(text), lv_subject_get_int(subject), format->decimals, format->unit);
    lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

/**
 * Frees the binding's format when the label is deleted.
 *
 * @param event The delete event; its user data is the format
 */
static void onLabelDeleted(lv_event_t* event) {
    delete static_cast<FixedPointFormat*>(lv_event_get_user_data(event));
}

void bindFixedPoint(lv_obj_t* label, lv_subject_t* subject, uint8_t decimals, const char* unit) {
    FixedPointFormat* format = new FixedPointFormat {decimals, unit};

    lv_obj_add_event_cb(label, onLabelDeleted, LV_EVENT_DELETE, format);
    lv_subject_add_observer_obj(subject, onFixedPointChanged, label, format);
}

void formatFixedPoint(char* buffer, size_t bufferSize, int32_t value, uint8_t decimals, const char* unit) {
    // Nine decimals is plenty for any drive unit and keeps the divisor far inside int64
    decimals = decimals > MAX_DECIMALS ? MAX_DECIMALS : decimals;
    int64_t divisor = 1;

    for (uint8_t place = 0; place < decimals; place++) {
        divisor *= 10;
    }

    // Split the magnitude so a value like -0.05 keeps its sign
    const char* sign = value < 0 ? "-" : "";
    int64_t magnitude = std::llabs(static_cast<int64_t>(value));
    int64_t whole = magnitude / divisor;
    int64_t fraction = magnitude % divisor;
    const char* separator = (unit != nullptr && unit[0] != '\0') ? " " : "";

    if (decimals == 0) {
        snprintf(buffer, bufferSize, "%s%" PRId64 "%s%s", sign, whole, separator, unit != nullptr ? unit : "");

        return;
    }

    snprintf(buffer, bufferSize, "%s%" PRId64 ".%0*" PRId64 "%s%s", sign, whole, static_cast<int>(decimals), fraction, separator, unit != nullptr ? unit : "");
}
void observeAll(lv_obj_t* target, lv_observer_cb_t callback, void* userData, std::initializer_list<lv_subject_t*> subjects) {
    for (lv_subject_t* subject : subjects) {
        lv_subject_add_observer_obj(subject, callback, target, userData);
    }
}
} // namespace Bindings
