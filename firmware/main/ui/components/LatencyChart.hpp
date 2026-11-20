#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates the latency chart (D-034, plan D8): during a run, one dot per sample in order, LED-on and LED-off edges in two
 * colours, with the running median as a line; after a run, a histogram of the samples, where frame quantisation shows
 * as clumps a frame period apart; before any run, a short placement guide.
 *
 * Follows the latency status's version; the samples come from the latency service.
 *
 * @param parent Container; the panel fills what the caller gives it
 * @param context UI context; outlives the chart
 * @return The panel
 */
lv_obj_t* createLatencyChart(lv_obj_t* parent, UiContext& context);
