#pragma once

#include <Arduino.h>

/**
 * Open-loop beam angle generator for characterizing the actuator.
 *
 * Two shapes, because they answer different questions:
 *
 * - A square wave gives the step response: deadband, backlash on reversal, slew
 *   rate, overshoot. Run it small (0.2-1 deg) as well as large — a regulator
 *   lives in the small-signal regime, and a servo that looks crisp on a 4 deg
 *   step can still creep for half a second on a 0.2 deg one.
 *
 * - A sine gives the frequency response, which is what actually sets the closed
 *   loop's stability margin. Sweep it across the intended crossover (about
 *   0.3 Hz for this rig) and read gain and phase off the plot. In closed loop
 *   the servo never sees a step — it sees a setpoint moving every tick — so this
 *   is the more honest test of the two.
 */
class MotionTest {
  public:
    enum class Mode {
        Off,
        Square,
        Sine,
    };

    /**
     * Starts a square wave, beginning at the positive amplitude.
     *
     * @param amplitudeDegrees Peak angle either side of level.
     * @param holdMs           How long each side is held.
     * @param nowMs            Current time.
     */
    void startSquare(float amplitudeDegrees, unsigned long holdMs, unsigned long nowMs);

    /**
     * Starts a sine sweep at a fixed frequency.
     *
     * @param amplitudeDegrees Peak angle either side of level.
     * @param frequencyHz      Cycles per second.
     * @param nowMs            Current time.
     */
    void startSine(float amplitudeDegrees, float frequencyHz, unsigned long nowMs);

    /** Stops generating; the beam is left wherever the last sample put it. */
    void stop();

    /** @returns the active shape. */
    Mode mode() const {
        return mode_;
    }

    /** @returns true while a test is generating angles. */
    bool isRunning() const {
        return mode_ != Mode::Off;
    }

    /**
     * @param nowMs Current time.
     * @returns The beam angle this test wants at this instant, or 0 when off.
     */
    float angleAt(unsigned long nowMs) const;

    /**
     * Prints a one-line description of the running test.
     *
     * @param out Stream to print to.
     */
    void describe(Print& out) const;

  private:
    Mode mode_ = Mode::Off;
    float amplitudeDegrees_ = 0.0f;
    unsigned long holdMs_ = 1;
    float frequencyHz_ = 0.0f;
    unsigned long startMs_ = 0;
};
