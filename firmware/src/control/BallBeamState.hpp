#pragma once

/**
 * Everything a controller is allowed to see about the rig, sampled at one
 * control tick.
 *
 * Deliberately expressed in physical units rather than sensor units: position in
 * millimetres along the beam, not raw time-of-flight distance. That keeps
 * controllers independent of which sensor produced the number, and means a
 * controller written against this struct survives a sensor change.
 */
struct BallBeamState {
    // Estimated ball position along the beam, in mm from the sensor endstop.
    float positionMm = 0.0f;

    // Estimated ball velocity, positive moving away from the sensor.
    float velocityMmPerSecond = 0.0f;

    // Where the ball should be.
    float targetMm = 0.0f;

    // Beam angle actually measured this tick, 0 = level.
    float beamAngleDegrees = 0.0f;

    // Time since the previous tick. Passed rather than assumed so a controller
    // never has to hard-code the loop rate.
    float dtSeconds = 0.0f;

    // False when the ball's position is not known — lifted off the beam, out of
    // the sensor's range, or too many readings rejected in a row. A controller
    // must not integrate or differentiate across an invalid stretch.
    bool isValid = false;

    /** @returns the position error, positive when the ball is past the target. */
    float error() const {
        return positionMm - targetMm;
    }
};
