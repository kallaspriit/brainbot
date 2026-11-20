#pragma once

#include "ui/Screen.hpp"

/**
 * The drive at a glance (UC-1, UC-2): live measurements as tiles, then identity, settings and stop values, and the
 * CiA 402 state with the statusword bits decoded. Read only; everything is bound to UiState.
 */
class OverviewScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

  private:
    /**
     * Builds the row of measurement tiles.
     *
     * @param parent Content area
     */
    void createTiles(lv_obj_t* parent);

    /**
     * Builds the identity panel.
     *
     * @param parent Row of panels
     */
    void createIdentityPanel(lv_obj_t* parent);

    /**
     * Builds the settings panel: communication, limits and stop values.
     *
     * @param parent Row of panels
     */
    void createSettingsPanel(lv_obj_t* parent);

    /**
     * Builds the state panel: CiA 402 state, statusword and its bits.
     *
     * @param parent Row of panels
     */
    void createStatePanel(lv_obj_t* parent);
};
