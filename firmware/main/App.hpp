#pragma once

#include "BoardConfig.hpp"
#include "debug/DebugCommands.hpp"
#include "drivers/CanBus.hpp"
#include "drivers/LightSensor.hpp"
#include "drivers/SerialConsole.hpp"
#include "protocol/SdoClient.hpp"
#include "services/LatencyService.hpp"
#include "services/MotorService.hpp"
#include "services/SystemStatus.hpp"
#include "ui/ScreenManager.hpp"
#include "ui/UiContext.hpp"
#include "ui/UiState.hpp"

/**
 * The console: owns every part and starts them in order.
 *
 * Members are declared in dependency order, so each one's constructor arguments already exist. begin() starts them:
 * display, theme, services, UI, serial console. A part that fails to start is logged and recorded in systemStatus,
 * and the rest still starts, so the screen can say what's wrong.
 */
class App {
  public:
    App();

    /**
     * Starts everything. Call once from app_main.
     */
    void begin();

    // State of the parts, for the UI
    SystemStatus systemStatus;

  private:
    // Drivers
    CanBus canBus;
    SerialConsole serialConsole;
    LightSensor lightSensor;

    // Protocol and services
    SdoClient sdoClient;
    MotorService motorService;
    LatencyService latencyService;

    // UI: the context refers to the screen manager declared after it, which only binds a reference
    UiState uiState;
    UiContext uiContext;
    ScreenManager screenManager;

    // Debug tools
    DebugCommands debugCommands;

    /**
     * Starts the CAN controller, the motor task and the latency task.
     */
    void startServices();

    /**
     * Copies the service snapshots into UiState; the bridge between the services and the UI (D-015).
     *
     * @param timer The bridge timer; its user data is the App
     */
    static void onUiStateTimer(lv_timer_t* timer);

    /**
     * Applies the theme, starts UiState and its bridge timer, and opens the home screen, holding the display lock.
     *
     * @param display The started display
     */
    void startUi(lv_display_t* display);

    // Constants
    static constexpr uint32_t UI_STATE_UPDATE_INTERVAL_MS = 100; // Matches the motor polling rate
};
