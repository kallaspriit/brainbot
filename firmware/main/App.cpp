#include "App.hpp"

#include "debug/TankTest.hpp"
#include "drivers/Display.hpp"
#include "drivers/DisplayLock.hpp"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "ui/Theme.hpp"
#include "ui/components/Splash.hpp"

static const char* TAG = "App";

App::App()
    : canBus(BoardConfig::PIN_CAN_TX, BoardConfig::PIN_CAN_RX, BoardConfig::CAN_BITRATE)
    , serialConsole(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM))
    , lightSensor(BoardConfig::PIN_LIGHT_SENSOR)
    , sdoClient(canBus, BoardConfig::DRIVE_NODE_ID, BoardConfig::SDO_TIMEOUT_MS)
    , motorService(sdoClient, canBus)
    , latencyService(lightSensor, BoardConfig::PIN_LATENCY_LED)
    , uiContext {uiState, motorService, screenManager, latencyService}
    , screenManager(uiContext)
    , debugCommands(serialConsole, screenManager, uiState, motorService, latencyService) {
}

void App::begin() {
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    ESP_LOGI(TAG, "Sparky starting on ESP32-P4 rev %d.%d", chipInfo.revision / 100, chipInfo.revision % 100);

    // Display first: without it there is nothing to show errors on, but the motor side still starts
    lv_display_t* display = Display::start();
    systemStatus.isDisplayStarted = display != nullptr;

    if (!systemStatus.isDisplayStarted) {
        ESP_LOGE(TAG, "Starting the display failed");
    }

    startServices();

    if (systemStatus.isDisplayStarted) {
        startUi(display);
    }

    // THROWAWAY (test/gamepad): the tank test borrows the SDO client while the motor service lends out the bus
    tankTestBegin(sdoClient, motorService);
    debugCommands.registerCommands();

    if (serialConsole.begin() != ESP_OK) {
        ESP_LOGE(TAG, "Starting the serial console failed");
    }

    // THROWAWAY (test/gamepad): tank mode from power-on, for driving on battery without USB; it stays disarmed until
    // LB + RB, and `tank off` hands the bus back
    debugCommands.startTankMode();
}

void App::startServices() {
    // The latency tool needs no motor or CAN bus (D-034)
    if (!latencyService.begin()) {
        ESP_LOGE(TAG, "Starting the latency service failed");
    }

    systemStatus.isCanStarted = canBus.begin() == ESP_OK;

    if (!systemStatus.isCanStarted) {
        ESP_LOGE(TAG, "Starting the CAN bus failed");

        return;
    }

    if (!motorService.begin()) {
        ESP_LOGE(TAG, "Starting the motor service failed");
    }
}

void App::startUi(lv_display_t* display) {
    // LVGL runs in its own task, so build the UI while holding its lock
    DisplayLock lock(DisplayLock::WAIT_FOREVER);

    if (!lock.isLocked()) {
        return;
    }

    Theme::init(display);

    // State first, so the first screen's bindings see real values
    uiState.init();
    uiState.update(motorService.getStatus(), systemStatus);
    lv_timer_create(onUiStateTimer, UI_STATE_UPDATE_INTERVAL_MS, this);

    screenManager.begin();
    showSplash();
}

void App::onUiStateTimer(lv_timer_t* timer) {
    App* app = static_cast<App*>(lv_timer_get_user_data(timer));
    app->uiState.update(app->motorService.getStatus(), app->systemStatus);

    // The latency status carries up to 200 samples: copied into a static rather than onto the LVGL task's stack
    static LatencyStatus latencyStatus;
    latencyStatus = app->latencyService.getStatus();
    app->uiState.updateLatency(latencyStatus);
}
