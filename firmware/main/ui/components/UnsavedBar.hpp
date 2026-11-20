#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the unsaved-changes bar of the Tuning, Parameters and Calibrate screens (PAR-005, D-033): how many parameters were written since the
 * last save, FACTORY RESET, REVERT and SAVE. Always shown, so nothing moves when the first change comes; the buttons
 * are disabled while they would do nothing. SAVE is off while the motor runs and opens a confirm that lists each change
 * and what else the save keeps (D-028: the console's own values go back first, battery values when the bus is too low
 * for the motor's own). FACTORY RESET is shown only where setUnsavedBarFactoryResetShown() says.
 *
 * @param parent Parent object (a flex column)
 * @param context Motor service, state and navigation; outlives the bar
 * @return The bar
 */
lv_obj_t* createUnsavedBar(lv_obj_t* parent, UiContext& context);

/**
 * Shows or hides FACTORY RESET: the Parameters tab has it; Tuning has no use for it and Setup has its own.
 *
 * @param bar The bar from createUnsavedBar()
 * @param isShown True to show it
 */
void setUnsavedBarFactoryResetShown(lv_obj_t* bar, bool isShown);
