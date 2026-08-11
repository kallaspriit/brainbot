#pragma once

#include <stdint.h>

#include "Config.hpp"
#include "SerialConsole.hpp"
#include "St3215.hpp"
#include "hardware/BeamServo.hpp"

/**
 * Console commands for driving and configuring servos on the bus directly.
 *
 * These are bench tools rather than part of the control path: scanning, poking
 * individual registers, checking feedback. Every handler takes the hardware lock
 * around its bus access, since it runs on core 0 while the control tick is
 * driving the beam from core 1.
 */
class ServoCommands {
  public:
    /**
     * @param servo Driver for the shared servo bus.
     * @param beam  Beam actuator, kept in step when its servo is moved directly.
     */
    ServoCommands(St3215& servo, BeamServo& beam)
        : servo_(servo),
          beam_(beam) {
    }

    /**
     * Registers every servo command.
     *
     * @param console Console to register with.
     */
    void registerCommands(SerialConsole& console);

    /** Pings every ID up to the scan limit and lists the ones that respond. */
    void scanBus();

  private:
    St3215& servo_;
    BeamServo& beam_;

    // Servos discovered by the last scan; used by "stop" so no motor is missed.
    uint8_t discoveredIds_[Config::kMaxScanId] = {};
    uint8_t discoveredCount_ = 0;

    void printServoInfo(uint8_t id);
    void printFeedback(uint8_t id);
};
