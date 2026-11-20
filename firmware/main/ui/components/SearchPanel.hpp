#pragma once

#include "lvgl.h"
#include "ui/UiState.hpp"

/**
 * Creates the "Looking for a motor" panel: a spinner, where the search is (D-026) and what to check. It shows itself
 * only while no drive is connected (D-033: on Overview, since the home grid is always visible).
 *
 * @param parent Container to add the panel to; the panel fills its width
 * @param state UI state; outlives the panel
 * @return The panel
 */
lv_obj_t* createSearchPanel(lv_obj_t* parent, UiState& state);
