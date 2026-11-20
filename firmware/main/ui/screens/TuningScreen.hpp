#pragma once

#include "ui/Screen.hpp"

/**
 * Tuning (D-033, moved from the Tune & parameters tabs): the live chart and the step test on the left, the tuning
 * settings with − / + steppers on the right (Parameters plan D8), and the unsaved bar with SAVE and REVERT above.
 * Leaving the screen ends a step test.
 */
class TuningScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;
};
