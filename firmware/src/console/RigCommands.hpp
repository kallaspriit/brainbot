#pragma once

#include "BallBeamSystem.hpp"
#include "SerialConsole.hpp"
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
     */
    RigCommands(BallBeamSystem& system, TelemetryStream& telemetry)
        : system_(system),
          telemetry_(telemetry) {
    }

    /**
     * Registers every rig command.
     *
     * @param console Console to register with.
     */
    void registerCommands(SerialConsole& console);

  private:
    BallBeamSystem& system_;
    TelemetryStream& telemetry_;
};
