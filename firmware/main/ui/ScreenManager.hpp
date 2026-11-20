#pragma once

#include <memory>
#include <vector>

#include "lvgl.h"
#include "ui/Navigator.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenRegistry.hpp"
#include "ui/UiContext.hpp"
#include "ui/components/TopBar.hpp"

/**
 * Owns the top bar, the overlay layer and the current screen, and switches between screens from the screen registry.
 *
 * Implements Navigator with a back stack. All methods run in the LVGL task (or with the display lock held).
 */
class ScreenManager : public Navigator {
  public:
    /**
     * Creates the manager; nothing is shown until begin().
     *
     * @param context State, motor commands and navigation, passed on to screens
     */
    explicit ScreenManager(UiContext& context);

    /**
     * Creates the top bar and the overlay layer, and opens the home screen.
     */
    void begin();

    // Navigator

    bool open(ScreenId screenId) override;

    void back() override;

    void home() override;

    lv_obj_t* getOverlayLayer() const override;

  private:
    UiContext& context;
    std::unique_ptr<TopBar> topBar;
    lv_obj_t* overlayLayer = nullptr;
    std::unique_ptr<Screen> currentScreen;
    ScreenId currentScreenId = ScreenId::HOME;
    ScreenId pendingScreenId = ScreenId::HOME; // Screen a queued open will show
    bool isOpenPending = false;                // An open is queued for the next LVGL cycle
    std::vector<ScreenId> history;             // Screens to return to with back(), oldest first

    /**
     * Returns the screen navigation is heading to: the queued one if an open is pending, otherwise the current one.
     *
     * @return The target screen
     */
    ScreenId getTargetScreenId() const;

    /**
     * Opens a screen on the next LVGL cycle, without touching the history. Several calls in one cycle queue one open,
     * of the last screen asked for.
     *
     * @param screenId Screen to open
     */
    void openDeferred(ScreenId screenId);

    /**
     * Replaces the current screen with a new one.
     *
     * @param definition Registry row of the screen to open
     */
    void openNow(const ScreenDefinition& definition);

    /**
     * Runs a deferred open.
     *
     * @param context The ScreenManager
     */
    static void onAsyncOpen(void* context);

    // Constants
    static constexpr size_t MAX_HISTORY = 8; // Deeper histories forget the oldest screens
};
