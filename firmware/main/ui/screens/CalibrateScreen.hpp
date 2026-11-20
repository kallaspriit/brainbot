#pragma once

#include "ui/Screen.hpp"

/**
 * Calibrate (D-032, a screen of its own since D-033): the drive's offline parameter identification with the stored and
 * identified constants, and the unsaved bar whose SAVE keeps a result and REVERT discards it.
 */
class CalibrateScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;
};
