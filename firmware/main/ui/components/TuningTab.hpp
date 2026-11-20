#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the Tuning tab (PAR-009, D8 of the Parameters plan): on the left the chart of the running motion (position
 * or torque while that mode runs, velocity otherwise), on the right the tuning list with its gain steppers. Motion started
 * from the Velocity or Position screen shows here, so gains can be stepped while watching the response.
 *
 * @param parent Parent object (a flex column); the tab fills its free height
 * @param context Motor service, state and navigation; outlives the tab
 * @return The tab's container
 */
lv_obj_t* createTuningTab(lv_obj_t* parent, UiContext& context);
