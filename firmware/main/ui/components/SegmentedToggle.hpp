#pragma once

#include <cstdint>

#include "lvgl.h"

/**
 * Called when the user picks the other option of a segmented toggle.
 *
 * @param index The picked option, 0 or 1
 * @param userData SegmentedToggleOptions::userData
 */
using SegmentedToggleCallback = void (*)(int32_t index, void* userData);

/**
 * Options for createSegmentedToggle().
 */
struct SegmentedToggleOptions {
    const char* first = "";                     // Option 0
    const char* second = "";                    // Option 1
    lv_subject_t* subject = nullptr;            // Integer subject holding the picked option; the toggle follows it
    SegmentedToggleCallback onChange = nullptr; // Called with the picked option
    void* userData = nullptr;                   // Passed to onChange; must outlive the toggle
    bool isStretched = false;                   // Fill the width the caller gives the toggle, segments sharing it equally; otherwise as wide as the labels
};

/**
 * Creates a two-option segmented toggle: two joined buttons, the picked one in the accent colour. For mode switches,
 * not motion commands, so it may be smaller than the 64 px touch target.
 *
 * Tapping an option reports it and sets the subject right away, as NumberControl does.
 *
 * @param parent Parent object
 * @param options Labels, subject and change callback
 * @return The toggle
 */
lv_obj_t* createSegmentedToggle(lv_obj_t* parent, const SegmentedToggleOptions& options);
