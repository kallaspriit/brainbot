#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates a scrolling chart of the actual and target position in turns over the last 10 s: a TrendChart fed from
 * MotorService's motion samples. The range follows the values without reaching for zero, snapped to half turns, so a
 * move far from the console zero still fills the chart. Size is left to the caller.
 *
 * @param parent Parent object
 * @param context Motor for the samples, state for the sample count
 * @return The chart's container
 */
lv_obj_t* createPositionChart(lv_obj_t* parent, UiContext& context);
