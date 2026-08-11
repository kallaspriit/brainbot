#pragma once

#include <stdint.h>

/** A single distance measurement of the ball along the beam. */
struct BallMeasurement {
    uint16_t distanceMm = 0;      // Distance from the sensor's endstop
    unsigned long timestampMs = 0; // When the reading was taken
    bool isValid = false;          // False if the sensor failed or was out of range
};

/**
 * Source of ball position measurements.
 *
 * Implementations are not thread safe and are shared between both cores — the
 * control tick polls them, and console commands reconfigure them. Every call
 * must be made holding the HardwareLock.
 *
 * Exists as an interface because the single VL53L0X at one endstop is the weak
 * link in this rig: its noise is worst at the far end of the beam, exactly where
 * control authority matters most. A second sensor at the opposite endstop would
 * fix that — two readings whose sum is a known constant give a free validity
 * check and an always-close-range measurement — and it should drop in behind
 * this interface without the estimator or any controller noticing.
 */
class IBallSensor {
  public:
    virtual ~IBallSensor() = default;

    /**
     * Initializes the sensor and starts continuous ranging.
     *
     * @returns true if the sensor responded.
     */
    virtual bool begin() = 0;

    /**
     * Polls for a completed measurement. Non-blocking: returns false when the
     * sensor has nothing new, which is most calls, since the control tick runs
     * faster than the sensor produces readings.
     *
     * @param out Destination measurement, written only when a reading completed.
     * @returns true when a new measurement was produced.
     */
    virtual bool read(BallMeasurement& out) = 0;

    /** @returns true if the sensor was detected at startup. */
    virtual bool isPresent() const = 0;

    /** @returns a short human-readable name for console output. */
    virtual const char* name() const = 0;

    /**
     * Sets how long the sensor integrates each measurement. Longer integration
     * trades update rate for noise, and where the optimum sits is the whole
     * question for this rig — a slower, quieter sensor is not automatically
     * better when the derivative term needs the latency.
     *
     * @param budgetUs Integration time in microseconds.
     * @returns false if this sensor has no such control.
     */
    virtual bool setTimingBudgetUs(uint32_t budgetUs) {
        (void)budgetUs;

        return false;
    }

    /** @returns the integration time in microseconds, or 0 if not applicable. */
    virtual uint32_t timingBudgetUs() {
        return 0;
    }
};
