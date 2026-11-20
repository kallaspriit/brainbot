#pragma once

#include "ui/Screen.hpp"

/**
 * Debug screen showing every UI font and size, the symbols they cover and every icon, for checking font conversion
 * on the real display. Not on home; opened with the `specimen` or `open specimen` serial command. Tap to go back.
 */
class SpecimenScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

  private:
    /**
     * Goes back when the specimen is tapped.
     *
     * @param event The click event; its user data is the screen's Navigator
     */
    static void onClicked(lv_event_t* event);
};
