#pragma once

#include <stdint.h>

/**
 * Two-state Kalman filter estimating the ball's position and velocity.
 *
 * The rig cannot produce a usable velocity by differencing sensor readings. The
 * measured noise is 13.5 mm at the centre of the beam, which a raw finite
 * difference at 27 Hz turns into roughly 500 mm/s of fabricated velocity —
 * larger than anything the ball actually does. Any derivative term fed from that
 * would command the beam to its limits with the ball sitting still.
 *
 * What makes the filter work here is that we already know most of what the ball
 * is about to do: the beam angle is our own output, and a tilted beam accelerates
 * the ball at a known rate. So the predict step is not a guess, and the
 * measurements only have to correct it. That buys smooth velocity at far less
 * lag than any amount of low-pass filtering could.
 *
 * Two details earn their place:
 *
 * - Measurement noise is a function of distance, not a constant. The sensor is
 *   six times noisier at the far end of the beam than at the middle, and a filter
 *   told otherwise would trust readings there that deserve no trust.
 *
 * - Measurements far from the prediction are rejected rather than absorbed. The
 *   sensor's error distribution has heavier tails than a Gaussian, and a single
 *   wild reading otherwise puts a step into the velocity estimate. Persistent
 *   rejection means the filter, not the sensor, is the thing that is wrong, so it
 *   reinitializes.
 */
class BallEstimator {
  public:
    /** Runtime-tunable parameters, adjustable from the console during tuning. */
    struct Params {
        float accelPerDegree;
        float processNoiseMmPerS2;
        float sensorNoiseBaseMm;
        float sensorNoiseQuadratic;
        float innovationGateSigma;
    };

    BallEstimator();

    /** Discards all state; the next measurement reinitializes the filter. */
    void reset();

    /**
     * Advances the estimate to now, using the beam angle as a known input.
     *
     * @param dtSeconds        Time since the previous call.
     * @param beamAngleDegrees Beam angle over that interval, 0 = level.
     */
    void predict(float dtSeconds, float beamAngleDegrees);

    /**
     * Folds in a distance measurement.
     *
     * @param measuredMm Measured distance from the sensor endstop.
     * @returns false if the measurement was rejected as an outlier.
     */
    bool correct(float measuredMm);

    /** @returns the estimated position in mm, meaningless until initialized. */
    float positionMm() const {
        return positionMm_;
    }

    /** @returns the estimated velocity in mm/s, positive away from the sensor. */
    float velocityMmPerSecond() const {
        return velocityMmPerSecond_;
    }

    /** @returns true once the filter has a measurement to stand on. */
    bool isInitialized() const {
        return isInitialized_;
    }

    /** @returns the current one-sigma uncertainty in the position estimate. */
    float positionSigmaMm() const;

    /** @returns the current one-sigma uncertainty in the velocity estimate. */
    float velocitySigmaMmPerS() const;

    /** @returns how many measurements in a row have been rejected. */
    uint8_t consecutiveRejects() const {
        return consecutiveRejects_;
    }

    /** @returns the tunable parameters. */
    Params& params() {
        return params_;
    }

    /**
     * Expected measurement noise at a given distance.
     *
     * @param distanceMm Distance being measured.
     * @returns One-sigma noise in mm.
     */
    float sensorSigmaAt(float distanceMm) const;

  private:
    Params params_;

    float positionMm_ = 0.0f;
    float velocityMmPerSecond_ = 0.0f;

    // Covariance, row-major. Symmetric, but kept in full because the asymmetric
    // intermediate in the update step is easier to read than the packed form.
    float p00_ = 0.0f;
    float p01_ = 0.0f;
    float p10_ = 0.0f;
    float p11_ = 0.0f;

    bool isInitialized_ = false;
    uint8_t consecutiveRejects_ = 0;

    /** Restarts the filter centred on a measurement, with no velocity. */
    void initializeAt(float measuredMm);

    /**
     * Holds the estimate on the beam. A ball resting against an endstop is not
     * still accelerating, however hard the model believes it is.
     */
    void constrainToBeam();
};
