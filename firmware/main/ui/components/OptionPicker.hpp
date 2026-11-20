#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"

/**
 * One choice in an option picker.
 */
struct PickerOption {
    int32_t value;     // What is picked
    const char* label; // What the user reads
};

/**
 * Called with the value the user picked.
 *
 * @param value The picked option's value
 * @param userData OptionPickerOptions::userData
 */
using OptionPickerCallback = void (*)(int32_t value, void* userData);

/**
 * Options for openOptionPicker().
 */
struct OptionPickerOptions {
    const char* title = "";                // What is being picked
    const char* subtitle = "";             // One line under the title; empty for none
    const PickerOption* options = nullptr; // The choices; must outlive the picker
    size_t optionCount = 0;                // Number of choices
    int32_t current = 0;                   // Value shown as picked; may match none
    OptionPickerCallback onPick = nullptr; // Called with the pick, after the picker starts closing
    void* userData = nullptr;              // Passed to onPick; must outlive the picker
};

/**
 * Opens a dialog with one full-width button per choice, the current one in the accent colour. A tap picks and closes;
 * the X or a tap outside closes without a pick. The list scrolls when long.
 *
 * @param overlayLayer Navigator::getOverlayLayer()
 * @param options Title, choices, the current value and the pick callback
 */
void openOptionPicker(lv_obj_t* overlayLayer, const OptionPickerOptions& options);
