#pragma once

#include <cstdint>
#include <initializer_list>

#include "lvgl.h"

/**
 * Helpers that bind widgets to UiState subjects, for formats LVGL's own bindings don't cover.
 *
 * Bindings are removed by LVGL together with the widget. For plain integers and strings use LVGL directly, e.g.
 * lv_label_bind_text(label, &state.motor.velocityRpm, "%d rpm").
 */
namespace Bindings {
/**
 * Shows an integer subject holding a fixed-point value as a decimal number with a unit.
 *
 * Example: busVoltageDeciV = 182 with decimals 1 and unit "V" shows "18.2 V"; -5 with decimals 2 and "A" shows
 * "-0.05 A".
 *
 * @param label Label to update
 * @param subject Integer subject in units of 10^-decimals
 * @param decimals Number of decimal places the subject's unit has (1 for 0.1 V, 2 for 0.01 A)
 * @param unit Unit text after the number, a string literal or other string that outlives the label; may be empty
 */
void bindFixedPoint(lv_obj_t* label, lv_subject_t* subject, uint8_t decimals, const char* unit);

/**
 * Formats a fixed-point value, as bindFixedPoint() shows it.
 *
 * @param buffer Output buffer
 * @param bufferSize Size of the buffer
 * @param value Value in units of 10^-decimals
 * @param decimals Number of decimal places, at most 9
 * @param unit Unit text, may be empty
 */
void formatFixedPoint(char* buffer, size_t bufferSize, int32_t value, uint8_t decimals, const char* unit);

/**
 * Runs one observer callback whenever any of several subjects changes, for a widget that shows a combination of them.
 *
 * LVGL removes the observers with the target widget. The user data must outlive the widget (UiState, never a screen).
 *
 * @param target Widget the callback updates
 * @param callback Observer callback; lv_observer_get_target_obj() gives the target
 * @param userData Passed to the callback through lv_observer_get_user_data()
 * @param subjects Subjects whose changes run the callback
 */
void observeAll(lv_obj_t* target, lv_observer_cb_t callback, void* userData, std::initializer_list<lv_subject_t*> subjects);
} // namespace Bindings
