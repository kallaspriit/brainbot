#pragma once

#include <vector>

#include "ui/Screen.hpp"

/**
 * The home screen: a 3×3 grid of cards, one per feature, shown with or without a drive (D-033). Cards whose screen needs
 * a motor are disabled while none answers; the search state is on Overview.
 */
class HomeScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

  private:
    // Grid templates; LVGL keeps pointers to them, so they live as long as the screen
    std::vector<int32_t> columnTemplate;
    std::vector<int32_t> rowTemplate;

    /**
     * Opens the screen of a tapped card.
     *
     * @param event The click event; its user data is the ScreenId
     */
    static void onCardClicked(lv_event_t* event);

    /**
     * Builds the grid of cards.
     *
     * @param parent Content area
     */
    void createCards(lv_obj_t* parent);

    // Constants
    static constexpr int GRID_COLUMNS = 3; // Three rows of three fill the screen (D-033)
};
