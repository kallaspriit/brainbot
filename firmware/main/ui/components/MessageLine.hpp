#pragma once

#include "lvgl.h"
#include "ui/UiState.hpp"

/**
 * Creates the line that shows the motor service's last message, with an icon and colour for its level: info in the
 * normal text colour, warning amber, error red. A new message fades in, even when it repeats the last one.
 *
 * Takes the font of its parent; width is 100 %, long messages wrap.
 *
 * @param parent Parent object
 * @param state Message, level and sequence subjects; outlives the line
 * @return The line
 */
lv_obj_t* createMessageLine(lv_obj_t* parent, UiState& state);
