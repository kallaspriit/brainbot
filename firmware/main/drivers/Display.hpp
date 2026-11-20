#pragma once

#include "lvgl.h"

/**
 * Brings up the 7" MIPI-DSI display, touch and LVGL through Waveshare's board support package.
 */
class Display {
  public:
    /**
     * Starts the display, touch and the LVGL task. The backlight is left as the board lights it at power-up.
     *
     * @return The LVGL display, or nullptr if it failed to start
     */
    static lv_display_t* start();
};
