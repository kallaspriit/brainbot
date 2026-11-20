#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the Setup tab's list (PAR-007): the settings that matter on a bench or in a PTU, grouped (current limits,
 * speeds and ramps, fault reactions, inputs, homing, supply), each with a control that fits it: a number in its unit,
 * a named choice, or a fault reaction. Built from the parameter table and edited through the same run-time writes and
 * SAVE as the Parameters tab.
 *
 * Fills its parent's free height and scrolls. Keeps a copy of the values, freed with the list.
 *
 * @param parent Parent object (a flex column)
 * @param context Motor service, state and navigation; outlives the list
 * @return The list
 */
lv_obj_t* createSetupList(lv_obj_t* parent, UiContext& context);

/**
 * Creates the Tuning tab's list (PAR-009): speed loop, position loop, filters and gain switching, from the same
 * machinery as the Setup list. The loop gains have - / + steppers of about 10 % that write live.
 *
 * @param parent Parent object (a flex column)
 * @param context Motor service, state and navigation; outlives the list
 * @return The list
 */
lv_obj_t* createTuningList(lv_obj_t* parent, UiContext& context);
