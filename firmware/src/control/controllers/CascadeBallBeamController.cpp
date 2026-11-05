#include "control/controllers/CascadeBallBeamController.hpp"

#include "Config.hpp"

CascadeBallBeamController::CascadeBallBeamController()
    : positionGain_(Config::kCascadePositionGain),
      positionIntegral_(Config::kCascadePositionIntegral),
      maxVelocity_(Config::kCascadeMaxVelocityMmPerSecond),
      velocityGain_(Config::kCascadeVelocityGain),
      velocityIntegral_(Config::kCascadeVelocityIntegral) {
}

void CascadeBallBeamController::reset() {
    integralVelocityMmPerSecond_ = 0.0f;
    integralDegrees_ = 0.0f;
}

float CascadeBallBeamController::update(const BallBeamState& state) {
    const float errorMm = state.targetMm - state.positionMm;

    // Outer loop: how fast the ball should be closing the gap, capped so a large
    // setpoint change is approached at the same speed as a small one is corrected.
    float desiredVelocity = (positionGain_ * errorMm) + integralVelocityMmPerSecond_;

    if (desiredVelocity > maxVelocity_) {
        desiredVelocity = maxVelocity_;
    } else if (desiredVelocity < -maxVelocity_) {
        desiredVelocity = -maxVelocity_;
    } else if (positionIntegral_ != 0.0f && state.dtSeconds > 0.0f) {
        // Conditional integration: only while the demand is unsaturated. Winding
        // further once the ball is already being asked for everything it can have
        // buys nothing and takes just as long to unwind afterwards.
        integralVelocityMmPerSecond_ += positionIntegral_ * errorMm * state.dtSeconds;

        if (integralVelocityMmPerSecond_ > Config::kCascadeMaxIntegralVelocity) {
            integralVelocityMmPerSecond_ = Config::kCascadeMaxIntegralVelocity;
        } else if (integralVelocityMmPerSecond_ < -Config::kCascadeMaxIntegralVelocity) {
            integralVelocityMmPerSecond_ = -Config::kCascadeMaxIntegralVelocity;
        }
    }

    // Inner loop: tilt the beam by however much the ball is off that speed.
    const float velocityErrorMmPerSecond = desiredVelocity - state.velocityMmPerSecond;

    if (velocityIntegral_ != 0.0f && state.dtSeconds > 0.0f) {
        integralDegrees_ += velocityIntegral_ * velocityErrorMmPerSecond * state.dtSeconds;

        if (integralDegrees_ > Config::kPidMaxIntegralDegrees) {
            integralDegrees_ = Config::kPidMaxIntegralDegrees;
        } else if (integralDegrees_ < -Config::kPidMaxIntegralDegrees) {
            integralDegrees_ = -Config::kPidMaxIntegralDegrees;
        }
    }

    return (velocityGain_ * velocityErrorMmPerSecond) + integralDegrees_;
}

ControllerParam CascadeBallBeamController::param(size_t index) {
    switch (index) {
        case 0:
            return ControllerParam {"kpos", &positionGain_};

        case 1:
            return ControllerParam {"kipos", &positionIntegral_};

        case 2:
            return ControllerParam {"vmax", &maxVelocity_};

        case 3:
            return ControllerParam {"kvel", &velocityGain_};

        case 4:
            return ControllerParam {"kivel", &velocityIntegral_};

        default:
            return ControllerParam {};
    }
}
