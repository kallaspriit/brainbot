#include "BallBeamSystem.hpp"

#include <Arduino.h>

#include "Config.hpp"
#include "hardware/HardwareLock.hpp"

bool BallBeamSystem::begin() {
    const bool isBeamReady = beam_.begin();

    sensor_.begin();

    targetMm_ = Config::kBeamCentreMm;
    lastTickMs_ = millis();
    isReady_ = true;

    return isBeamReady;
}

void BallBeamSystem::pollSensor() {
    BallMeasurement measurement;

    if (!sensor_.read(measurement) || !measurement.isValid) {
        return;
    }

    const float positionMm = (float)measurement.distanceMm;

    // Deliberately a raw finite difference, not a filtered one. It is far too
    // noisy to differentiate a +-10 mm signal this way and feed the result to a
    // derivative term — at 33 Hz, 10 mm of jitter becomes 300 mm/s of imaginary
    // velocity. Fixing that is the estimator's job, and it needs the raw
    // distribution to be characterized first, so nothing is smoothed here.
    if (hasPosition_ && measurement.timestampMs > lastValidMeasurementMs_) {
        const float intervalSeconds = (float)(measurement.timestampMs - lastValidMeasurementMs_) / 1000.0f;

        lastVelocityMmPerSecond_ = (positionMm - lastPositionMm_) / intervalSeconds;
    }

    lastPositionMm_ = positionMm;
    lastValidMeasurementMs_ = measurement.timestampMs;
    hasPosition_ = true;
}

void BallBeamSystem::tick() {
    const unsigned long nowMs = millis();

    // Sensor I2C is core 1's alone, so this needs no lock.
    pollSensor();

    const bool isStale = !hasPosition_ || (nowMs - lastValidMeasurementMs_) > kMeasurementTimeoutMs;
    const float dtSeconds = (float)(nowMs - lastTickMs_) / 1000.0f;

    lastTickMs_ = nowMs;

    const HardwareLock lock;

    St3215::ServoFeedback feedback;

    // Refreshed before the controller runs, so it acts on this tick's beam angle
    // rather than the previous one. Previous values are kept when a read fails,
    // so a dropped reply does not punch a spike into the plots.
    if (beam_.readFeedback(feedback)) {
        snapshot_.feedback = feedback;
    }

    state_.positionMm = lastPositionMm_;
    state_.velocityMmPerSecond = isStale ? 0.0f : lastVelocityMmPerSecond_;
    state_.targetMm = targetMm_;
    state_.beamAngleDegrees = beam_.positionToAngle(snapshot_.feedback.position);
    state_.dtSeconds = dtSeconds;
    state_.isValid = !isStale;

    // A relaxed beam is not commanded at all. Writing a goal position to a servo
    // with torque off would leave it queued, and the beam would snap there the
    // moment torque came back.
    if (beam_.isEnabled()) {
        // An open-loop test outranks the controller: a characterization run must
        // not be fought by a strategy someone forgot to switch off.
        if (motionTest_.isRunning()) {
            beam_.setAngle(motionTest_.angleAt(nowMs));
        } else if (controller_ != nullptr && state_.isValid) {
            beam_.setAngle(controller_->update(state_));
        } else if (controller_ != nullptr) {
            // Never act on a position we do not have. Level the beam and drop any
            // accumulated state so the controller restarts clean when the ball
            // reappears.
            controller_->reset();
            beam_.setAngle(0.0f);
        }
    }

    snapshot_.distanceMm = (uint16_t)lastPositionMm_;
    snapshot_.beamAngleDegrees = beam_.positionToAngle(snapshot_.feedback.position);
    snapshot_.targetAngleDegrees = beam_.targetAngle();
}

void BallBeamSystem::snapshot(TelemetrySnapshot& out) const {
    const HardwareLock lock;

    out = snapshot_;
}

BallBeamState BallBeamSystem::state() const {
    const HardwareLock lock;

    return state_;
}

void BallBeamSystem::setController(BallBeamController* controller) {
    const HardwareLock lock;

    controller_ = controller;

    if (controller_ != nullptr) {
        controller_->reset();
        beam_.enable();
    }
}

void BallBeamSystem::setTarget(float positionMm) {
    const HardwareLock lock;

    targetMm_ = positionMm;
}
