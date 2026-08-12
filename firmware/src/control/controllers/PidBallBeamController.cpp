#include "control/controllers/PidBallBeamController.hpp"

#include "Config.hpp"

PidBallBeamController::PidBallBeamController()
    : kp_(Config::kPidKp),
      ki_(Config::kPidKi),
      kd_(Config::kPidKd) {
}

void PidBallBeamController::reset() {
    integralDegrees_ = 0.0f;
}

float PidBallBeamController::update(const BallBeamState& state) {
    // Positive error means the ball must move away from the sensor, and a
    // positive beam angle accelerates it that way, so the proportional term is
    // positive. The damping term opposes whatever velocity the ball already has.
    const float errorMm = state.targetMm - state.positionMm;

    if (ki_ != 0.0f && state.dtSeconds > 0.0f) {
        integralDegrees_ += ki_ * errorMm * state.dtSeconds;

        if (integralDegrees_ > Config::kPidMaxIntegralDegrees) {
            integralDegrees_ = Config::kPidMaxIntegralDegrees;
        } else if (integralDegrees_ < -Config::kPidMaxIntegralDegrees) {
            integralDegrees_ = -Config::kPidMaxIntegralDegrees;
        }
    }

    return (kp_ * errorMm) + integralDegrees_ - (kd_ * state.velocityMmPerSecond);
}

ControllerParam PidBallBeamController::param(size_t index) {
    switch (index) {
        case 0:
            return ControllerParam {"kp", &kp_};

        case 1:
            return ControllerParam {"ki", &ki_};

        case 2:
            return ControllerParam {"kd", &kd_};

        default:
            return ControllerParam {};
    }
}
