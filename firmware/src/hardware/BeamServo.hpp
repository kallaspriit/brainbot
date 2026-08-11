#pragma once

#include <stdint.h>

#include "St3215.hpp"

/**
 * The beam's actuator: an ST3215 coupled directly to the pivot, so servo angle
 * and beam angle are the same thing apart from a mechanical zero offset.
 *
 * Wraps the raw driver with the things the control loop actually wants — angles
 * in degrees rather than encoder counts, a zero trim, and a hard angle clamp —
 * and applies the servo's internal gains at boot so the rig's behaviour is
 * defined by this repository rather than by whatever is left in a particular
 * servo's EEPROM.
 *
 * None of these methods take the HardwareLock; callers hold it across whatever
 * sequence of calls they need.
 */
class BeamServo {
  public:
    /**
     * Encoder counts per revolution. The 12-bit magnetic encoder reports 0..4095,
     * which is 4096 distinct counts covering 360 deg, so a count is 0.0879 deg
     * and the centre lands on 2048.
     */
    static constexpr float kStepsPerRevolution = 4096.0f;
    static constexpr int16_t kCentrePosition = 2048;

    /**
     * @param servo Driver for the shared servo bus.
     * @param id    ID of the servo driving the beam.
     */
    BeamServo(St3215& servo, uint8_t id)
        : servo_(servo),
          id_(id) {
    }

    /**
     * Puts the servo into position mode and applies the configured gains and
     * deadband.
     *
     * Leaves the beam relaxed. Powering up into a hold would snap the beam to
     * whatever goal position was left in the servo's SRAM, which is both a
     * surprise and a way to fling the ball across the bench; the beam only takes
     * torque when something explicitly asks it to move.
     *
     * @returns true if the servo acknowledged the configuration.
     */
    bool begin();

    /**
     * Takes torque, holding wherever the beam currently is. Sets the goal to the
     * present position before enabling, so enabling never causes a jump.
     *
     * @returns true if the servo acknowledged.
     */
    bool enable();

    /** @returns true if the beam has been enabled since boot. */
    bool isEnabled() const {
        return isEnabled_;
    }

    /**
     * Commands a beam angle. The value is clamped to the configured limit and
     * rounded to the nearest encoder count, so the angle actually commanded may
     * differ from the request by up to half a step.
     *
     * @param angleDegrees Desired beam angle, 0 = level, positive = right side up.
     * @returns true if the servo acknowledged.
     */
    bool setAngle(float angleDegrees);

    /** @returns the last angle commanded, after clamping and rounding. */
    float targetAngle() const {
        return targetAngle_;
    }

    /**
     * Reads live feedback from the servo.
     *
     * @param out Destination feedback struct.
     * @returns true if the read succeeded.
     */
    bool readFeedback(St3215::ServoFeedback& out);

    /** Stops the beam where it is, leaving torque as it was. */
    bool hold();

    /** Releases torque so the beam free-wheels. */
    bool relax();

    /**
     * Sets the mechanical zero offset: the servo angle at which the beam is
     * actually level. Applied to both commanded and measured angles, so 0 deg
     * means level on both sides of the loop.
     *
     * @param angleDegrees Offset in degrees.
     */
    void setTrim(float angleDegrees) {
        trimDegrees_ = angleDegrees;
    }

    /** @returns the mechanical zero offset in degrees. */
    float trim() const {
        return trimDegrees_;
    }

    /** @returns the servo ID driving the beam. */
    uint8_t id() const {
        return id_;
    }

    /**
     * Converts a beam angle to an absolute encoder position, applying the trim.
     * Rounds to the nearest count rather than truncating, which would bias
     * positive angles down and negative angles up by half a step each.
     *
     * @param angleDegrees Beam angle.
     * @returns Encoder position.
     */
    int16_t angleToPosition(float angleDegrees) const;

    /**
     * Converts an encoder position back to a beam angle, removing the trim.
     *
     * @param position Encoder position.
     * @returns Beam angle in degrees.
     */
    float positionToAngle(int position) const;

  private:
    St3215& servo_;
    uint8_t id_;
    float trimDegrees_ = 0.0f;
    float targetAngle_ = 0.0f;
    bool isEnabled_ = false;
};
