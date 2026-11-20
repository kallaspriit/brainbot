#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the torque chart (Torque mode plan, D2): the torque target and the torque from the motor current, both in
 * mN·m from the rated torque, with the velocity on the second scale. The drive reports the current without a sign
 * (bench 2026-10-12), so both are drawn as magnitudes; the velocity shows the direction.
 *
 * @param parent Parent object
 * @param context Motor service for the samples; outlives the chart
 * @return The chart
 */
lv_obj_t* createTorqueChart(lv_obj_t* parent, UiContext& context);
