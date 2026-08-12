#pragma once

#include "control/BallBeamController.hpp"

/**
 * Two nested loops: position error sets a desired ball velocity, and velocity
 * error sets the beam angle.
 *
 * The reason to prefer this over a single PID on a double integrator is the
 * limit in the middle. A PID answers "you are 200 mm away" with a large angle and
 * holds it until the error shrinks, by which point the ball is travelling fast
 * and carries straight past the setpoint — the overshoot is structural, not a
 * tuning fault. Capping the desired velocity means the ball approaches at a rate
 * the inner loop can still arrest, so a large setpoint change and a small one
 * behave the same way at the end.
 *
 * It is also easier to tune, because each loop can be adjusted against a
 * quantity you can watch directly: the outer against how briskly the ball closes
 * the gap, the inner against how well it holds a commanded speed.
 */
class CascadeBallBeamController : public BallBeamController {
  public:
    CascadeBallBeamController();

    const char* name() const override {
        return "cascade";
    }

    void reset() override;

    float update(const BallBeamState& state) override;

    size_t paramCount() const override {
        return 4;
    }

    ControllerParam param(size_t index) override;

  private:
    float positionGain_;
    float maxVelocity_;
    float velocityGain_;
    float velocityIntegral_;

    float integralDegrees_ = 0.0f;
};
