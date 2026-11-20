#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"

/**
 * Called when the user picks a new value.
 *
 * @param value The new value, already clamped to the control's range
 * @param userData NumberControlOptions::userData
 */
using NumberControlCallback = void (*)(int32_t value, void* userData);

/**
 * Options for createNumberControl().
 */
struct NumberControlOptions {
    const char* label = "";                   // Name in the header
    const char* unit = "";                    // Unit after the value
    lv_subject_t* subject = nullptr;          // Integer subject holding the value; the control follows it
    int32_t min = 0;                          // Smallest value
    int32_t max = 100;                        // Largest value, until maxSubject sets it
    lv_subject_t* maxSubject = nullptr;       // Optional: the largest value follows this subject
    bool isSymmetric = false;                 // The smallest value is -max (for signed targets)
    lv_obj_t* overlayLayer = nullptr;         // Navigator::getOverlayLayer(); tapping the value opens a number pad
    uint8_t decimals = 0;                     // Fixed-point decimals of the subject (3 for milliturns shown as turns)
    bool hasSlider = true;                    // A slider for coarse changes; off for unbounded values like positions
    const int32_t* steps = nullptr;           // Step buttons, e.g. {-10, -1, 1, 10}; must outlive the control
    size_t stepCount = 0;                     // Number of steps
    NumberControlCallback onChange = nullptr; // Called with every new value
    void* userData = nullptr;                 // Passed to onChange; must outlive the control
};

/**
 * Creates a number control: a header with the name and the value in fixed-width digits, a slider for coarse changes,
 * and a row of step buttons for precise ones.
 *
 * The control shows the subject's value and reports changes through onChange; it also sets the subject right away,
 * so the change shows before the owner of the value confirms it. With an overlay layer, tapping the header value opens
 * a number pad for typing a value. Width is left to the caller.
 *
 * @param parent Parent object
 * @param options Name, unit, subject, range, steps and change callback
 * @return The control
 */
lv_obj_t* createNumberControl(lv_obj_t* parent, const NumberControlOptions& options);

/**
 * Returns a number control's header row (label, value, unit), for adding widgets such as a mode toggle in place of
 * the label: create them in the header, then lv_obj_move_to_index() to place one first. With an empty label, the
 * label still takes the free space, keeping the value on the right.
 *
 * @param control Control from createNumberControl()
 * @return The header row
 */
lv_obj_t* getNumberControlHeader(lv_obj_t* control);
