#pragma once

#include "control/BallBeamController.hpp"

/**
 * Proportional-integral-derivative control of ball position via beam angle.
 *
 * The derivative term uses the estimator's velocity rather than differencing the
 * position error, which matters more here than it usually would. Differencing
 * this sensor produces roughly 500 mm/s of noise against real ball speeds of a
 * few hundred, so a conventional PID would command the beam to its limits with
 * the ball sitting still. The estimator brings that to about 20 mm/s.
 *
 * Taking velocity from the estimator also means no derivative kick: moving the
 * setpoint changes the error instantly, but the ball's velocity does not, so the
 * output moves smoothly rather than spiking.
 */
class PidBallBeamController : public BallBeamController {
  public:
    PidBallBeamController();

    const char* name() const override {
        return "pid";
    }

    void reset() override;

    float update(const BallBeamState& state) override;

    size_t paramCount() const override {
        return 3;
    }

    ControllerParam param(size_t index) override;

  private:
    float kp_;
    float ki_;
    float kd_;

    // Accumulated in degrees rather than as a raw error integral, so the clamp is
    // expressed in the units it needs to be safe in, and changing ki mid-session
    // does not rescale everything already accumulated.
    float integralDegrees_ = 0.0f;
};
