#pragma once

#include "control/BallBeamController.hpp"
#include "control/BallBeamState.hpp"
#include "hardware/BallSensor.hpp"
#include "hardware/BeamServo.hpp"
#include "motion/MotionTest.hpp"
#include "telemetry/Telemetry.hpp"

/**
 * The rig itself: sensor, estimator, actuator, and whichever control strategy is
 * currently installed, driven by one fixed-rate tick on core 1.
 *
 * The tick is the only thing that commands the beam. An open-loop motion test
 * takes priority over the controller so a characterization run cannot be fought
 * by a controller someone left installed.
 */
class BallBeamSystem {
  public:
    /**
     * @param beam   Beam actuator.
     * @param sensor Ball position source.
     */
    BallBeamSystem(BeamServo& beam, IBallSensor& sensor)
        : beam_(beam),
          sensor_(sensor) {
    }

    /**
     * Brings up the hardware. Runs on core 0 before core 1 starts, so the boot
     * output stays in one piece and the tick never sees half-configured
     * hardware.
     *
     * @returns true if the beam servo configured successfully.
     */
    bool begin();

    /** @returns true once begin() has completed and ticking is safe. */
    bool isReady() const {
        return isReady_;
    }

    /** Runs one control cycle. Called from core 1 at a fixed rate. */
    void tick();

    /**
     * Copies the latest telemetry state.
     *
     * @param out Destination snapshot.
     */
    void snapshot(TelemetrySnapshot& out) const;

    /**
     * Installs a control strategy, or none. Passing nullptr leaves the beam
     * wherever it is rather than levelling it, so switching controllers does not
     * fling the ball.
     *
     * @param controller Strategy to install, or nullptr for open loop.
     */
    void setController(BallBeamController* controller);

    /** @returns the installed strategy, or nullptr. */
    BallBeamController* controller() const {
        return controller_;
    }

    /**
     * Sets where the ball should be held.
     *
     * @param positionMm Distance from the sensor endstop.
     */
    void setTarget(float positionMm);

    /** @returns the ball's target position in mm. */
    float target() const {
        return targetMm_;
    }

    /** @returns the beam actuator, for console commands that drive it directly. */
    BeamServo& beam() {
        return beam_;
    }

    /** @returns the ball sensor. */
    IBallSensor& sensor() {
        return sensor_;
    }

    /** @returns the open-loop test generator. */
    MotionTest& motionTest() {
        return motionTest_;
    }

    /** @returns the most recent ball state handed to a controller. */
    BallBeamState state() const;

  private:
    // Ball position is treated as unknown after this long without a valid
    // reading — the ball has been lifted off, or has rolled somewhere the sensor
    // cannot see it. Long enough to ride out a few rejected readings, short
    // enough that a controller does not keep acting on stale data.
    static constexpr unsigned long kMeasurementTimeoutMs = 200;

    BeamServo& beam_;
    IBallSensor& sensor_;
    MotionTest motionTest_;
    BallBeamController* controller_ = nullptr;

    bool isReady_ = false;
    float targetMm_ = 0.0f;

    unsigned long lastTickMs_ = 0;
    unsigned long lastValidMeasurementMs_ = 0;
    float lastPositionMm_ = 0.0f;
    float lastVelocityMmPerSecond_ = 0.0f;
    bool hasPosition_ = false;

    BallBeamState state_;
    TelemetrySnapshot snapshot_;

    /** Drains the sensor, updating the position estimate from any new reading. */
    void pollSensor();
};
