#pragma once

#include "lvgl.h"
#include "ui/UiState.hpp"

/**
 * Creates a small battery drawn from shapes for the top bar: an outline whose fill shows the pack level estimated from
 * the bus voltage (the drive's 200B:15, so only with a motor connected), green, amber when low, red at the pack cutoff
 * (D-005). Above any 18 V pack's voltage it shows a bolt instead: an external supply.
 *
 * The level comes from voltage alone, which sags under load, so it's coarse by design. A proper pack voltage sense
 * belongs to M5 (power wiring).
 *
 * @param parent Parent object
 * @param state Bus voltage and cutoff subjects; outlives the indicator
 * @return The indicator
 */
lv_obj_t* createBatteryIndicator(lv_obj_t* parent, UiState& state);
