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
    restartRanging();

    return true;
}

bool Vl53l0xBallSensor::setTimingBudgetUs(uint32_t budgetUs) {
    if (!isPresent_) {
        return false;
    }

    sensor_.stopRangeContinuous();

    if (!sensor_.setMeasurementTimingBudgetMicroSeconds(budgetUs)) {
        restartRanging();

        return false;
    }

    restartRanging();

    return true;
}

uint32_t Vl53l0xBallSensor::timingBudgetUs() {
    if (!isPresent_) {
        return 0;
    }

    return sensor_.getMeasurementTimingBudgetMicroSeconds();
}

bool Vl53l0xBallSensor::setProfile(Profile profile) {
    if (!isPresent_) {
        return false;
    }

    Adafruit_VL53L0X::VL53L0X_Sense_config_t config = Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT;

    switch (profile) {
        case Profile::LongRange:
            config = Adafruit_VL53L0X::VL53L0X_SENSE_LONG_RANGE;

            break;

        case Profile::HighSpeed:
            config = Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_SPEED;

            break;

        case Profile::HighAccuracy:
            config = Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_ACCURACY;

            break;

        case Profile::Default:
        default:
            break;
    }

    sensor_.stopRangeContinuous();

    const bool isOk = sensor_.configSensor(config);

    restartRanging();

    return isOk;
}

void Vl53l0xBallSensor::restartRanging() {
    // The inter-measurement period must leave room for the integration time,
    // otherwise a long timing budget silently keeps the old rate.
    const uint32_t budgetMs = sensor_.getMeasurementTimingBudgetMicroSeconds() / 1000;
    const uint32_t periodMs = budgetMs + 4 > Config::kRangingPeriodMs ? budgetMs + 4 : Config::kRangingPeriodMs;

    sensor_.startRangeContinuous((uint16_t)periodMs);
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
