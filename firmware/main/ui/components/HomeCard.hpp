#pragma once

#include "lvgl.h"

/**
 * Options for createHomeCard().
 */
struct HomeCardOptions {
    const char* icon = "";           // Icons:: constant
    const char* title = "";          // Card title
    const char* status = "";         // One line under the title
    bool isEnabled = true;           // Disabled cards are faded and don't respond
    lv_event_cb_t onClick = nullptr; // Click handler
    void* userData = nullptr;        // Passed to the handler as event user data
};

/**
 * Creates a home screen card: a large icon, a title and a one-line status.
 *
 * Size is left to the caller (grid cell or lv_obj_set_size).
 *
 * @param parent Parent object
 * @param options Icon, texts, enabled state and click handler
 * @return The card
 */
lv_obj_t* createHomeCard(lv_obj_t* parent, const HomeCardOptions& options);
