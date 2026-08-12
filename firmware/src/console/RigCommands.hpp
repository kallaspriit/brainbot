#pragma once

#include "BallBeamSystem.hpp"
#include "SerialConsole.hpp"
#include "hardware/Vl53l0xBallSensor.hpp"
#include "telemetry/TelemetryStream.hpp"

/**
 * Console commands for the rig as a whole: telemetry, direct beam control, and
 * the open-loop characterization tests.
 *
 * These talk to BallBeamSystem rather than to the servo bus, so they cannot
 * collide with the control tick — the tick remains the only thing that commands
 * the beam, and these only change what it is being asked to do.
 */
class RigCommands {
  public:
    /**
     * @param system    The rig.
     * @param telemetry Telemetry stream to enable and pace.
     * @param sensor    Concrete sensor, for the preset profiles that only exist
     *                  on the VL53L0X and so cannot sit behind IBallSensor.
     */
    RigCommands(BallBeamSystem& system, TelemetryStream& telemetry, Vl53l0xBallSensor& sensor)
        : system_(system),
          telemetry_(telemetry),
          sensor_(sensor) {
    }

    /**
     * Makes a control strategy selectable by name from the console.
     *
     * Registration rather than a hard-coded list, so adding a strategy is a line
     * in main() and nothing here changes.
     *
     * @param controller Strategy to offer.
     */
    void addController(BallBeamController* controller);

    /**
     * Registers every rig command.
     *
     * @param console Console to register with.
     */
    void registerCommands(SerialConsole& console);

  private:
    static constexpr size_t kMaxControllers = 6;

    BallBeamSystem& system_;
    TelemetryStream& telemetry_;
    Vl53l0xBallSensor& sensor_;

    BallBeamController* controllers_[kMaxControllers] = {};
    size_t controllerCount_ = 0;
};
