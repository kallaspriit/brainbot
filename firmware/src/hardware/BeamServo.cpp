#include "BeamServo.hpp"

#include <Arduino.h>
#include <math.h>

#include "Config.hpp"

bool BeamServo::begin() {
    const bool okMode = servo_.setMode(id_, St3215::Mode::Position);
    const bool okPid = servo_.setPid(id_, Config::kBeamServoKp, Config::kBeamServoKd, Config::kBeamServoKi);
    const bool okDeadband = servo_.setDeadband(id_, Config::kBeamServoDeadband, Config::kBeamServoDeadband);

    relax();

    // Seed the commanded angle from where the beam actually is, so telemetry
    // does not claim a target of 0 while the beam hangs wherever gravity left it.
    const int position = servo_.readPosition(id_);

    if (position >= 0) {
        targetAngle_ = positionToAngle(position);
    }

    return okMode && okPid && okDeadband;
}

bool BeamServo::enable() {
    // Goal first, then torque: enabling against a stale goal position would snap
    // the beam across to wherever it was last told to go.
    const int position = servo_.readPosition(id_);

    if (position >= 0) {
        targetAngle_ = positionToAngle(position);
        servo_.writePos(id_, (int16_t)position);
    }

    isEnabled_ = servo_.setTorque(id_, true);

    return isEnabled_;
}

bool BeamServo::setAngle(float angleDegrees) {
    const float limit = Config::kMaxBeamAngleDegrees;
    const float clamped = angleDegrees > limit ? limit : (angleDegrees < -limit ? -limit : angleDegrees);
    const int16_t position = angleToPosition(clamped);

    // Report what the beam was actually asked for, not what the caller wanted,
    // so telemetry shows the commanded angle including clamping and rounding.
    targetAngle_ = positionToAngle(position);

    return servo_.writePos(id_, position);
}

bool BeamServo::readFeedback(St3215::ServoFeedback& out) {
    return servo_.readFeedback(id_, out);
}

bool BeamServo::hold() {
    return servo_.stop(id_);
}

bool BeamServo::relax() {
    isEnabled_ = false;

    return servo_.setTorque(id_, false);
}

int16_t BeamServo::angleToPosition(float angleDegrees) const {
    const float steps = (angleDegrees + trimDegrees_) * (kStepsPerRevolution / 360.0f);

    return (int16_t)(lroundf(steps) + kCentrePosition);
}

float BeamServo::positionToAngle(int position) const {
    return ((float)(position - kCentrePosition)) * (360.0f / kStepsPerRevolution) - trimDegrees_;
}
