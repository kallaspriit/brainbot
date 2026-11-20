#include "ScreenManager.hpp"

#include "esp_log.h"
#include "ui/Theme.hpp"

static const char* TAG = "ScreenManager";

ScreenManager::ScreenManager(UiContext& context)
    : context(context) {
}

void ScreenManager::begin() {
    topBar = std::make_unique<TopBar>(context);

    // Overlays sit above the top bar; the layer itself doesn't take touches, so it's invisible until used
    overlayLayer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlayLayer);
    lv_obj_set_size(overlayLayer, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(overlayLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlayLayer, LV_OBJ_FLAG_SCROLLABLE);

    openNow(ScreenRegistry::get(ScreenId::HOME));
}

bool ScreenManager::open(ScreenId screenId) {
    const ScreenDefinition& definition = ScreenRegistry::get(screenId);

    if (definition.factory == nullptr) {
        ESP_LOGW(TAG, "Screen \"%s\" isn't built yet", definition.name);

        return false;
    }

    // Compare with where navigation is heading, so two taps before the screen changes don't stack the same entry
    ScreenId targetScreenId = getTargetScreenId();

    if (screenId == targetScreenId) {
        return true;
    }

    // Remember where we came from, forgetting the oldest entry when the history is full
    if (history.size() >= MAX_HISTORY) {
        history.erase(history.begin());
    }

    history.push_back(targetScreenId);
    openDeferred(screenId);

    return true;
}

void ScreenManager::back() {
    if (history.empty()) {
        home();

        return;
    }

    ScreenId previousScreenId = history.back();
    history.pop_back();
    openDeferred(previousScreenId);
}

void ScreenManager::home() {
    history.clear();
    openDeferred(ScreenId::HOME);
}

lv_obj_t* ScreenManager::getOverlayLayer() const {
    return overlayLayer;
}

ScreenId ScreenManager::getTargetScreenId() const {
    return isOpenPending ? pendingScreenId : currentScreenId;
}

void ScreenManager::openDeferred(ScreenId screenId) {
    pendingScreenId = screenId;

    if (isOpenPending) {
        return;
    }

    if (lv_async_call(onAsyncOpen, this) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Queueing the screen change failed");

        return;
    }

    isOpenPending = true;
}

void ScreenManager::openNow(const ScreenDefinition& definition) {
    // The old screen gets to act (e.g. stop the motor) before anything else happens
    if (currentScreen != nullptr) {
        currentScreen->onLeave();
    }

    lv_obj_clean(overlayLayer);

    // New LVGL screen with a content area below the top bar
    lv_obj_t* lvScreen = lv_obj_create(nullptr);
    lv_obj_add_style(lvScreen, &Theme::screenStyle, 0);
    lv_obj_remove_flag(lvScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* content = lv_obj_create(lvScreen);
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content, 0, Theme::TOP_BAR_HEIGHT);
    lv_obj_set_size(content, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - Theme::TOP_BAR_HEIGHT);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    std::unique_ptr<Screen> nextScreen = definition.factory(context);
    nextScreen->create(content);

    // Load the new screen, which deletes the old one's widgets right away (no animation), then the old Screen
    // object, so nothing it owns is gone while its widgets still exist
    std::unique_ptr<Screen> previousScreen = std::move(currentScreen);
    currentScreen = std::move(nextScreen);
    currentScreenId = definition.id;
    lv_screen_load_anim(lvScreen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
    previousScreen.reset();

    topBar->setTitle(definition.title);
    topBar->setHome(definition.id == ScreenId::HOME);
    topBar->setMotorPartOptional(definition.hidesMotorPart);
    currentScreen->onEnter();

    ESP_LOGI(TAG, "Opened %s (history %u)", definition.name, static_cast<unsigned>(history.size()));
}

void ScreenManager::onAsyncOpen(void* context) {
    ScreenManager* manager = static_cast<ScreenManager*>(context);
    manager->isOpenPending = false;

    // Navigation may have come back to where it started before this ran
    if (manager->currentScreen != nullptr && manager->pendingScreenId == manager->currentScreenId) {
        return;
    }

    manager->openNow(ScreenRegistry::get(manager->pendingScreenId));
}
