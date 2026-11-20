#pragma once

#include <cstdint>
#include <vector>

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * Base class for a screen: builds its widgets into the content area below the top bar.
 *
 * Lifecycle: constructed and create() when opened, onEnter() once it is showing, onLeave() just before it is
 * replaced, then deleted. LVGL deletes the widgets with the screen; timers added with addTimer() are deleted by this
 * class first, so no timer outlives the widgets it updates.
 */
class Screen {
  public:
    /**
     * Creates the screen object; widgets are built later by create().
     *
     * @param context State, motor commands and navigation
     */
    explicit Screen(UiContext& context);

    /**
     * Deletes the screen's timers.
     */
    virtual ~Screen();

    /**
     * Builds the screen's widgets. Runs in the LVGL task.
     *
     * @param content Container below the top bar, sized to the rest of the display
     */
    virtual void create(lv_obj_t* content) = 0;

    /**
     * Called once the screen is showing.
     */
    virtual void onEnter() {
    }

    /**
     * Called just before the screen is replaced. The motor keeps running across screens (D-016).
     */
    virtual void onLeave() {
    }

  protected:
    UiContext& context;

    /**
     * Adds a timer owned by this screen, deleted when the screen is.
     *
     * @param callback Timer callback; lv_timer_get_user_data() returns this screen
     * @param periodMs Period in milliseconds
     * @return The timer
     */
    lv_timer_t* addTimer(lv_timer_cb_t callback, uint32_t periodMs);

  private:
    std::vector<lv_timer_t*> timers;
};
