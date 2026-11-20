#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"

/**
 * Called when the user picks a tab.
 *
 * @param index The picked tab
 * @param userData TabStripOptions::userData
 */
using TabStripCallback = void (*)(int32_t index, void* userData);

/**
 * Options for createTabStrip().
 */
struct TabStripOptions {
    const char* const* labels = nullptr; // Tab labels, kept by the caller for the strip's lifetime
    size_t count = 0;                    // Number of tabs
    int32_t selected = 0;                // Tab shown first
    TabStripCallback onChange = nullptr; // Called with the picked tab
    void* userData = nullptr;            // Passed to onChange; must outlive the strip
};

/**
 * Creates a row of tabs: text buttons with the picked one in the accent colour and underlined, on a divider line.
 * Full touch-target height. Flat, so it sits over a report without adding a box.
 *
 * @param parent Parent object
 * @param options Labels, the first tab and the change callback
 * @return The strip
 */
lv_obj_t* createTabStrip(lv_obj_t* parent, const TabStripOptions& options);
