#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Called with the number the user confirmed.
 *
 * @param value The value, inside the pad's range
 * @param userData NumberPadOptions::userData
 */
using NumberPadCallback = void (*)(int32_t value, void* userData);

/**
 * Options for openNumberPad().
 */
struct NumberPadOptions {
    const char* title = "";                // What is being entered, e.g. "Target speed"
    const char* unit = "";                 // Unit after the number
    int32_t value = 0;                     // Current value, shown until the first key
    int32_t min = 0;                       // Smallest accepted value; a negative min adds a ± key
    int32_t max = 0;                       // Largest accepted value
    uint8_t decimals = 0;                  // Fixed-point decimals: values are in units of 10^-decimals, and a decimal point key appears
    NumberPadCallback onConfirm = nullptr; // Called on OK with a value inside the range
    void* userData = nullptr;              // Passed to onConfirm; must outlive the pad
};

/**
 * Opens a modal number pad on the overlay layer: the range, the typed number, digit keys, ± when negatives are
 * allowed, a decimal point when the pad has decimals, backspace, Cancel and OK.
 *
 * OK with a number outside the range doesn't close the pad but turns the range red. Cancel, or a tap outside the
 * pad, closes it without a change. Navigation empties the overlay layer, which closes the pad too.
 *
 * @param overlayLayer Navigator::getOverlayLayer()
 * @param options Title, unit, current value, range and confirm callback
 */
void openNumberPad(lv_obj_t* overlayLayer, const NumberPadOptions& options);
