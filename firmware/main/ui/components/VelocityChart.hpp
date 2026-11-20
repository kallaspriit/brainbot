#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates a scrolling chart of the actual and target velocity over the last 10 s (D-017): a TrendChart fed from
 * MotorService's velocity samples.
 * The range snaps to 50 rpm. Size is left to the caller.
 *
 * @param parent Parent object
 * @param context Motor for the samples, state for the sample count
 * @return The chart's container
 */
lv_obj_t* createVelocityChart(lv_obj_t* parent, UiContext& context);
