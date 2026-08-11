#pragma once

#include <Arduino.h>

#include "hardware/BallSensor.hpp"

/**
 * Measures what the ball sensor actually does, with the ball held still.
 *
 * The number that matters is not the spread of the distance readings but the
 * spread of the differences between consecutive readings, because that is what a
 * derivative term amplifies. A sensor can look acceptable on a position plot and
 * still be useless for velocity: at 33 Hz, 10 mm of sample-to-sample jitter
 * becomes 330 mm/s of fabricated velocity, which is the same order as the real
 * thing. This test reports both, so the decision about filtering is made from a
 * measurement rather than an impression.
 *
 * Fed from the control tick on core 1; read from the console on core 0 once
 * complete, after which nothing mutates it.
 */
class SensorNoiseTest {
  public:
    /**
     * Begins a run, discarding any previous result.
     *
     * @param durationMs How long to sample for.
     * @param nowMs      Current time.
     */
    void start(unsigned long durationMs, unsigned long nowMs);

    /**
     * Advances the run. Call once per control tick.
     *
     * @param nowMs       Current time.
     * @param measurement New measurement, or nullptr when the sensor produced
     *                    nothing this tick.
     */
    void tick(unsigned long nowMs, const BallMeasurement* measurement);

    /** @returns true while sampling. */
    bool isRunning() const {
        return state_ == State::Running;
    }

    /** @returns true once a run has finished and the result can be read. */
    bool isComplete() const {
        return state_ == State::Complete;
    }

    /**
     * Prints the result. Only meaningful once isComplete() returns true.
     *
     * @param out            Stream to print to.
     * @param timingBudgetUs Sensor integration time, for the record.
     */
    void report(Print& out, uint32_t timingBudgetUs) const;

  private:
    enum class State {
        Idle,
        Running,
        Complete,
    };

    // Written by core 1, polled by core 0.
    volatile State state_ = State::Idle;

    unsigned long startMs_ = 0;
    unsigned long durationMs_ = 0;

    // Doubles rather than floats: summing squares of ~300 mm readings overruns a
    // float's mantissa within a few hundred samples, which would quietly corrupt
    // the variance this test exists to measure.
    double sum_ = 0.0;
    double sumSquares_ = 0.0;
    double stepSum_ = 0.0;
    double stepSumSquares_ = 0.0;

    uint32_t validCount_ = 0;
    uint32_t rejectedCount_ = 0;
    uint32_t stepCount_ = 0;

    uint16_t minMm_ = 0;
    uint16_t maxMm_ = 0;

    // Range of the readings that were thrown away. A reject just over the beam
    // length means a real measurement was clipped by kMaxDistanceMm and the
    // survivors are a biased sample; a reject of 8190 means the sensor genuinely
    // failed to range. The two call for completely different fixes.
    uint16_t minRejectedMm_ = 0;
    uint16_t maxRejectedMm_ = 0;

    float lastDistanceMm_ = 0.0f;
    unsigned long lastTimestampMs_ = 0;
    unsigned long firstTimestampMs_ = 0;
    bool hasPrevious_ = false;
};
