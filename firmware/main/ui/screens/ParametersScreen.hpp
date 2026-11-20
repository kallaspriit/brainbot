#pragma once

#include "ui/Screen.hpp"

/**
 * The motor's settings (M6, Parameters plan): Setup, the curated settings and node settings, and Parameters, every
 * parameter in the table by group (PAR-003). Tuning and Calibrate were tabs here until D-033 made them screens.
 */
class ParametersScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

  private:
    lv_obj_t* tabContent = nullptr; // Holds the picked tab's widgets
    lv_obj_t* unsavedBar = nullptr; // Above the tabs; FACTORY RESET only on the Parameters tab

    /**
     * Replaces the tab content with the picked tab.
     *
     * @param index 0 Setup, 1 Parameters
     */
    void showTab(int32_t index);

    /**
     * Tab strip callback.
     *
     * @param index Picked tab
     * @param userData The screen; the strip is its widget, so it can't outlive it
     */
    static void onTabChanged(int32_t index, void* userData);
};
