#pragma once

#include <stddef.h>

#include "control/BallBeamState.hpp"

/**
 * One runtime-tunable controller parameter, exposed so the console can list and
 * change gains without a reflash. Tuning a ball & beam is a matter of dozens of
 * small adjustments; a rebuild between each one is the difference between an
 * evening's work and a week's.
 */
struct ControllerParam {
    const char* name = nullptr;
    float* value = nullptr;
};

/**
 * A strategy for keeping the ball where it should be.
 *
 * Implementations map ball state to a desired beam angle and nothing else — no
 * hardware access, no timing, no telemetry. That keeps them small enough to read
 * in one sitting and directly comparable against each other, since swapping
 * strategies changes only which object is installed.
 *
 * The system clamps the returned angle to the beam's safe range, so an
 * implementation may return whatever its maths produces.
 */
class BallBeamController {
  public:
    virtual ~BallBeamController() = default;

    /** @returns the name used to select this controller from the console. */
    virtual const char* name() const = 0;

    /**
     * Clears all internal state — integrators, filters, previous samples.
     * Called when the controller is installed and whenever the ball's position
     * becomes unknown, so stale state never carries across a gap.
     */
    virtual void reset() = 0;

    /**
     * Computes the beam angle for this tick.
     *
     * @param state Current ball and beam state.
     * @returns Desired beam angle in degrees, 0 = level.
     */
    virtual float update(const BallBeamState& state) = 0;

    /** @returns how many tunable parameters this controller exposes. */
    virtual size_t paramCount() const {
        return 0;
    }

    /**
     * @param index Parameter index, below paramCount().
     * @returns The parameter, or an empty one if the index is out of range.
     */
    virtual ControllerParam param(size_t index) {
        (void)index;

        return ControllerParam {};
    }
};
