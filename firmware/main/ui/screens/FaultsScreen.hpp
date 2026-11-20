#pragma once

#include "ui/Screen.hpp"

/**
 * What is wrong with the drive and what it remembers (UC-3, Faults plan): the active fault with its name, the reaction
 * the drive is configured for (200E), the statusword's fault and warning bits and the error register, next to the alarm
 * history (1003, newest first). Opened from its home card and from the top bar's fault and warning badges (D1).
 */
class FaultsScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

  private:
    /**
     * Builds the active fault section: name, code, reaction, statusword bits and error register.
     *
     * @param parent Left column
     */
    void createActiveSection(lv_obj_t* parent);

    /**
     * Builds the session log section: the console's own record of EMCYs, resets and connects, with times (D-024).
     *
     * @param parent Left column
     */
    void createSessionLogSection(lv_obj_t* parent);

    /**
     * Builds the outcome message and the RESET FAULT and RESTORE DEFAULTS buttons.
     *
     * @param parent Left column
     */
    void createActions(lv_obj_t* parent);

    /**
     * Builds the alarm history section.
     *
     * @param parent Right column
     */
    void createHistorySection(lv_obj_t* parent);
};
