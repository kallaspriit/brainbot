#pragma once

#include "lvgl.h"

enum class ScreenId;

/**
 * Navigation as seen by screens and components: open a screen, go back, go home, and the overlay layer for dialogs
 * that navigation closes.
 *
 * Implemented by ScreenManager. Calls are deferred to the next LVGL cycle, so they are safe from a widget's event
 * handler on the screen being left. Call from the LVGL task or with the display lock held.
 */
class Navigator {
  public:
    virtual ~Navigator() = default;

    /**
     * Opens a screen, remembering the current one for back(). Opening the current screen again does nothing.
     *
     * @param screenId Screen to open
     * @return False if the screen isn't built yet
     */
    virtual bool open(ScreenId screenId) = 0;

    /**
     * Returns to the previous screen, or home if there is none.
     */
    virtual void back() = 0;

    /**
     * Opens the home screen and forgets the history.
     */
    virtual void home() = 0;

    /**
     * Returns the layer above the top bar for dialogs (the number pad, confirmations). It is emptied on every
     * navigation, so a dialog never outlives the screen that opened it.
     *
     * @return The overlay layer
     */
    virtual lv_obj_t* getOverlayLayer() const = 0;
};
