#pragma once

#include "lvgl.h"
#include "ui/UiState.hpp"

/**
 * Creates the light level bar for aiming the latency sensor (D-034, plan D5): the live brightness as a fill, its range
 * over the last 100 ms as a faint band (flicker shows as a wide band), and once a run has learned them, marks for the
 * dark level, the threshold and the bright level, with the contrast.
 *
 * @param parent Container; the panel fills its width
 * @param latency Latency subjects; outlive the panel
 * @return The panel
 */
lv_obj_t* createLevelBar(lv_obj_t* parent, UiState::Latency& latency);
