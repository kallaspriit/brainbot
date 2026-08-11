#pragma once

#include <Adafruit_VL53L0X.h>

#include "hardware/BallSensor.hpp"

/**
 * Ball sensor backed by a single VL53L0X time-of-flight module at one endstop,
 * running in continuous ranging mode.
 *
 * Deliberately does no smoothing. Out-of-range results are rejected because they
 * are not measurements at all — the VL53L0X signals a failed ranging with a
 * large value (8190 out of range, 65535 on error) rather than with an error —
 * but the genuine measurement noise is passed through untouched, so the
 * estimator sees the real distribution and characterization runs measure the
 * sensor rather than a filter's opinion of it.
 */
class Vl53l0xBallSensor : public IBallSensor {
  public:
    bool begin() override;
    bool read(BallMeasurement& out) override;

    bool isPresent() const override {
        return isPresent_;
    }

    const char* name() const override {
        return "VL53L0X";
    }

    /** @returns readings rejected as out of range since boot. */
    unsigned long rejectedCount() const {
        return rejectedCount_;
    }

  private:
    Adafruit_VL53L0X sensor_;
    bool isPresent_ = false;
    unsigned long rejectedCount_ = 0;
};
