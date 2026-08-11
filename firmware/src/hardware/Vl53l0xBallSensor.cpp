#include "hardware/Vl53l0xBallSensor.hpp"

#include <Arduino.h>

#include "Config.hpp"

bool Vl53l0xBallSensor::begin() {
    Wire1.setSDA(Config::kDistanceSensorSdaPin);
    Wire1.setSCL(Config::kDistanceSensorSclPin);

    if (!sensor_.begin(VL53L0X_I2C_ADDR, false, &Wire1, Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT)) {
        isPresent_ = false;

        return false;
    }

    isPresent_ = true;
    sensor_.startRangeContinuous(Config::kRangingPeriodMs);

    return true;
}

bool Vl53l0xBallSensor::read(BallMeasurement& out) {
    if (!isPresent_ || !sensor_.isRangeComplete()) {
        return false;
    }

    const uint16_t distanceMm = sensor_.readRangeResult();
    const bool hasTimedOut = sensor_.timeoutOccurred();

    out.timestampMs = millis();
    out.distanceMm = distanceMm;
    out.isValid = !hasTimedOut && distanceMm >= Config::kMinDistanceMm && distanceMm <= Config::kMaxDistanceMm;

    if (!out.isValid) {
        rejectedCount_++;
    }

    return true;
}
