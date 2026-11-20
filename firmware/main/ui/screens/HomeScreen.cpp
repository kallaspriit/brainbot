#include "HomeScreen.hpp"

#include <cstdint>

#include "ui/Navigator.hpp"
#include "ui/ScreenRegistry.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/HomeCard.hpp"

void HomeScreen::create(lv_obj_t* content) {
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Always shown (D-033): cards that need a motor fade while none is connected; the search is on Overview
    createCards(content);
}

void HomeScreen::createCards(lv_obj_t* parent) {
    // Rows follow the number of cards, so a new registry row never falls outside the grid
    int cardCount = 0;

    for (const ScreenDefinition& definition : ScreenRegistry::getAll()) {
        cardCount += definition.isOnHome ? 1 : 0;
    }

    int rowCount = (cardCount + GRID_COLUMNS - 1) / GRID_COLUMNS;
    columnTemplate.assign(GRID_COLUMNS, LV_GRID_FR(1));
    columnTemplate.push_back(LV_GRID_TEMPLATE_LAST);
    rowTemplate.assign(rowCount, LV_GRID_FR(1));
    rowTemplate.push_back(LV_GRID_TEMPLATE_LAST);

    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, columnTemplate.data(), rowTemplate.data());
    lv_obj_set_style_pad_row(grid, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_column(grid, Theme::SPACE_M, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE);

    // One card per registry row shown on home, in table order; screens not built yet are disabled
    int cardIndex = 0;

    for (const ScreenDefinition& definition : ScreenRegistry::getAll()) {
        if (!definition.isOnHome) {
            continue;
        }

        HomeCardOptions options;
        options.icon = definition.icon;
        options.title = definition.title;
        options.status = definition.cardStatus;
        options.isEnabled = definition.factory != nullptr;
        options.onClick = onCardClicked;
        options.userData = reinterpret_cast<void*>(static_cast<intptr_t>(definition.id));

        lv_obj_t* card = createHomeCard(grid, options);
        lv_obj_set_user_data(card, this);

        // A built screen that needs a motor is disabled while no drive answers (D-033)
        if (definition.factory != nullptr && definition.needsMotor) {
            lv_obj_bind_state_if_eq(card, &context.state.motor.isConnected, LV_STATE_DISABLED, 0);
        }

        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, cardIndex % GRID_COLUMNS, 1, LV_GRID_ALIGN_STRETCH, cardIndex / GRID_COLUMNS, 1);
        cardIndex++;
    }
}

void HomeScreen::onCardClicked(lv_event_t* event) {
    lv_obj_t* card = lv_event_get_current_target_obj(event);
    HomeScreen* screen = static_cast<HomeScreen*>(lv_obj_get_user_data(card));
    ScreenId screenId = static_cast<ScreenId>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));

    screen->context.navigator.open(screenId);
}
