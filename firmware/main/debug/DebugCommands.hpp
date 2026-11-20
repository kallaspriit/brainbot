#pragma once

#include <functional>

#include "drivers/SerialConsole.hpp"
#include "services/LatencyService.hpp"
#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/ScreenRegistry.hpp"
#include "ui/UiState.hpp"

/**
 * Serial commands for working on the console from a PC: screenshots, opening screens, the font specimen.
 *
 * Commands run in the serial console's task. Anything that touches LVGL takes the display lock first; that lock is
 * LVGL's own mutex, so holding it makes the call safe from this task.
 */
class DebugCommands {
  public:
    /**
     * Creates the commands; nothing is registered until registerCommands().
     *
     * @param console Serial console to register on
     * @param navigator Navigation, for opening screens
     * @param uiState UI state, for the `state` command
     * @param motor Motor service, for the motor and drive commands
     * @param latency Latency service, for the `latency` commands
     */
    DebugCommands(SerialConsole& console, Navigator& navigator, UiState& uiState, MotorService& motor, LatencyService& latency);

    /**
     * Registers every command on the console. Call before the console starts.
     */
    void registerCommands();

    /**
     * THROWAWAY (test/gamepad): starts Bluetooth, opens the gamepad view and turns tank mode on (disarmed), as
     * `tank on` does.
     */
    void startTankMode();

  private:
    SerialConsole& console;
    Navigator& navigator;
    UiState& uiState;
    MotorService& motor; // For dump (FLT-001, temporary)
    LatencyService& latency;

    /**
     * Parses the `sdo` command's arguments and queues the transfer.
     *
     * @param arguments The text after `sdo`
     */
    void handleSdoCommand(const std::string& arguments);

    /**
     * THROWAWAY (test/gamepad): runs a "tank" console command.
     *
     * @param arguments Text after "tank "
     */
    void handleTankCommand(const std::string& arguments);

    /**
     * Runs a `latency` subcommand: level, run, selftest, stop, off, led on, led off.
     *
     * @param arguments The text after `latency`
     */
    void handleLatencyCommand(const std::string& arguments);

    /**
     * Logs each task's CPU share over a short window, the heap, and how many LVGL objects and timers exist, to find
     * what slows the UI down.
     */
    void logPerformance();

    /**
     * Runs an action that touches the UI, holding the display lock.
     *
     * @param action What to run
     */
    void withDisplayLock(const std::function<void()>& action);
};
