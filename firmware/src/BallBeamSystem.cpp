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
    // Kept whether or not it is valid: the noise test needs the rejects too, and
    // a rejection rate is as much a property of the sensor as its variance is.
    hasNewMeasurement_ = sensor_.read(lastMeasurement_);

    if (!hasNewMeasurement_ || !lastMeasurement_.isValid) {
        return;
    }

    lastRawDistanceMm_ = (float)lastMeasurement_.distanceMm;
    lastValidMeasurementMs_ = lastMeasurement_.timestampMs;
    hasPosition_ = true;
}

void BallBeamSystem::tick() {
    const unsigned long nowMs = millis();

    const float dtSeconds = (float)(nowMs - lastTickMs_) / 1000.0f;

    lastTickMs_ = nowMs;

    const HardwareLock lock;

    // Under the lock, not outside it. The console can reconfigure the sensor
    // from core 0, and two cores driving the same I2C peripheral concurrently
    // corrupts transfers — which shows up not as a bad reading but as a device
    // left in a state it never recovers from.
    pollSensor();

    const BallMeasurement* newMeasurement = hasNewMeasurement_ ? &lastMeasurement_ : nullptr;

    noiseTest_.tick(nowMs, newMeasurement);
    rollTest_.tick(nowMs, newMeasurement);

    St3215::ServoFeedback feedback;

    // Refreshed before the estimator runs, so the predict step uses this tick's
    // beam angle rather than the previous one. Previous values are kept when a
    // read fails, so a dropped reply does not punch a spike into the plots.
    if (beam_.readFeedback(feedback)) {
        snapshot_.feedback = feedback;
    }

    const float beamAngleDegrees = beam_.positionToAngle(snapshot_.feedback.position);

    // Predict every tick, correct only when the sensor has actually produced
    // something. The control loop runs at 100 Hz and the sensor at 27, so most
    // ticks are prediction alone — which is the point of modelling the tilt.
    estimator_.predict(dtSeconds, beamAngleDegrees);

    if (newMeasurement != nullptr && newMeasurement->isValid) {
        estimator_.correct((float)newMeasurement->distanceMm);
    }

    const bool isStale = !hasPosition_ || !estimator_.isInitialized() || (nowMs - lastValidMeasurementMs_) > kMeasurementTimeoutMs;

    state_.positionMm = estimator_.positionMm();
    state_.velocityMmPerSecond = isStale ? 0.0f : estimator_.velocityMmPerSecond();
    state_.targetMm = targetMm_;
    state_.beamAngleDegrees = beamAngleDegrees;
    state_.dtSeconds = dtSeconds;
    state_.isValid = !isStale;

    if (isStale) {
        // Position unknown for long enough that the estimate is fiction. Drop it
        // so the filter restarts clean when the ball reappears rather than
        // integrating a stale velocity onwards.
        estimator_.reset();
    }

    // A relaxed beam is not commanded at all. Writing a goal position to a servo
    // with torque off would leave it queued, and the beam would snap there the
    // moment torque came back.
    if (beam_.isEnabled()) {
        // Characterization runs outrank the controller: a measurement must not be
        // fought by a strategy someone forgot to switch off.
        if (rollTest_.isRunning()) {
            beam_.setAngle(rollTest_.angleDegrees());
        } else if (motionTest_.isRunning()) {
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

    snapshot_.distanceMm = (uint16_t)lastRawDistanceMm_;
    snapshot_.beamAngleDegrees = beamAngleDegrees;
    snapshot_.targetAngleDegrees = beam_.targetAngle();
    snapshot_.estimatedPositionMm = state_.positionMm;
    snapshot_.estimatedVelocityMmPerSecond = state_.velocityMmPerSecond;
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
