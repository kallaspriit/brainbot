#pragma once

#include <Arduino.h>

#include "hardware/BallSensor.hpp"

/**
 * Measures the plant constant: how hard the ball accelerates per degree of beam
 * tilt, in mm/s^2 per degree.
 *
 * Theory says about 110 for a ball rolling without slipping in the extrusion
 * channel, but that assumes no slip and no rolling resistance, and it says
 * nothing at all about the sign — which way the ball rolls for a positive beam
 * angle depends on how the servo happens to be mounted. Both come out of one
 * measurement here, which is cheaper than reasoning about it and more reliable.
 *
 * Method: hold a fixed tilt, record where the ball is over the next second or
 * so, and fit a parabola to it. The quadratic coefficient is half the
 * acceleration. The fit solves for the initial velocity too, so the ball does
 * not have to start perfectly at rest.
 */
class BallRollTest {
  public:
    /**
     * Begins a run. The beam is held at the given angle for the duration.
     *
     * @param angleDegrees Tilt to hold; larger gives a cleaner fit but less
     *                     time before the ball reaches the end.
     * @param durationMs   How long to record for.
     * @param settleMs     Samples in this initial window are discarded, so that
     *                     stiction before the ball breaks away does not drag the
     *                     fitted acceleration down.
     * @param nowMs        Current time.
     */
    void start(float angleDegrees, unsigned long durationMs, unsigned long settleMs, unsigned long nowMs);

    /**
     * Advances the run. Call once per control tick.
     *
     * @param nowMs       Current time.
     * @param measurement New measurement, or nullptr if none this tick.
     */
    void tick(unsigned long nowMs, const BallMeasurement* measurement);

    /** Abandons a run in progress. */
    void stop();

    /** @returns true while the beam should be held at the test angle. */
    bool isRunning() const {
        return state_ == State::Running;
    }

    /** @returns true once a run has finished and the result can be read. */
    bool isComplete() const {
        return state_ == State::Complete;
    }

    /** @returns the tilt being held. */
    float angleDegrees() const {
        return angleDegrees_;
    }

    /**
     * Fits the samples and prints the result.
     *
     * @param out Stream to print to.
     */
    void report(Print& out) const;

  private:
    enum class State {
        Idle,
        Running,
        Complete,
    };

    volatile State state_ = State::Idle;

    float angleDegrees_ = 0.0f;
    unsigned long startMs_ = 0;
    unsigned long durationMs_ = 0;
    unsigned long settleMs_ = 0;

    // Normal-equation sums for a least-squares quadratic fit of position against
    // time. Doubles because the fourth power of the time span, summed over a few
    // hundred samples, has no business inside a float.
    double sumT_ = 0.0;
    double sumT2_ = 0.0;
    double sumT3_ = 0.0;
    double sumT4_ = 0.0;
    double sumY_ = 0.0;
    double sumTy_ = 0.0;
    double sumT2y_ = 0.0;

    uint32_t sampleCount_ = 0;
    uint32_t rejectedCount_ = 0;
    float firstMm_ = 0.0f;
    float lastMm_ = 0.0f;
};
