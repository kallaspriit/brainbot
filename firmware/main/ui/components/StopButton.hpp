#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Creates a STOP button bound to the motor: it requests a stop when tapped, is filled red while the drive can move
 * (power stage on, or a run active) and a red outline otherwise, and is disabled only without a drive.
 *
 * The look follows the drive's own state, not only the console's run flag, so a drive enabled by anything still gets
 * a loud STOP; the outline stays tappable for the same reason.
 *
 * @param parent Parent object
 * @param context Motor for the stop request, state for the look
 * @param isCompact Compact look (Theme::BUTTON_HEIGHT_COMPACT), still a full touch target
 * @return The button; width is left to the caller
 */
lv_obj_t* createStopButton(lv_obj_t* parent, UiContext& context, bool isCompact);
