#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the content of the Calibrate screen (Motor calibration plan, D-032; a screen since D-033): what the drive's offline parameter
 * identification will do with this motor, CALIBRATE behind a confirm dialog, the stage and speed while it runs, and the
 * result table (stored, identified, difference) afterwards. KEEP and DISCARD are the unsaved bar's SAVE and REVERT.
 *
 * @param parent The tab content holder
 * @param context Motor service, state and navigation; outlives the tab
 * @return The tab's root object
 */
lv_obj_t* createCalibrateTab(lv_obj_t* parent, UiContext& context);
