#include "control/BallEstimator.hpp"

#include <math.h>

#include "Config.hpp"

BallEstimator::BallEstimator() {
    params_.accelPerDegree = Config::kAccelPerDegree;
    params_.processNoiseMmPerS2 = Config::kProcessNoiseMmPerS2;
    params_.sensorNoiseBaseMm = Config::kSensorNoiseBaseMm;
    params_.sensorNoiseQuadratic = Config::kSensorNoiseQuadratic;
    params_.innovationGateSigma = Config::kInnovationGateSigma;

    reset();
}

void BallEstimator::reset() {
    positionMm_ = 0.0f;
    velocityMmPerSecond_ = 0.0f;
    p00_ = 0.0f;
    p01_ = 0.0f;
    p10_ = 0.0f;
    p11_ = 0.0f;
    isInitialized_ = false;
    consecutiveRejects_ = 0;
}

float BallEstimator::sensorSigmaAt(float distanceMm) const {
    return params_.sensorNoiseBaseMm + params_.sensorNoiseQuadratic * distanceMm * distanceMm;
}

float BallEstimator::positionSigmaMm() const {
    return p00_ > 0.0f ? sqrtf(p00_) : 0.0f;
}

float BallEstimator::velocitySigmaMmPerS() const {
    return p11_ > 0.0f ? sqrtf(p11_) : 0.0f;
}

void BallEstimator::initializeAt(float measuredMm) {
    const float sigma = sensorSigmaAt(measuredMm);

    positionMm_ = measuredMm;
    velocityMmPerSecond_ = 0.0f;

    p00_ = sigma * sigma;
    p01_ = 0.0f;
    p10_ = 0.0f;
    p11_ = Config::kInitialVelocitySigmaMmPerS * Config::kInitialVelocitySigmaMmPerS;

    isInitialized_ = true;
    consecutiveRejects_ = 0;
}

void BallEstimator::predict(float dtSeconds, float beamAngleDegrees) {
    if (!isInitialized_ || dtSeconds <= 0.0f) {
        return;
    }

    // The beam angle is our own output, so this is a known input rather than a
    // disturbance. Predicting with it is what keeps the velocity estimate both
    // smooth and prompt.
    const float accel = params_.accelPerDegree * beamAngleDegrees;

    positionMm_ += velocityMmPerSecond_ * dtSeconds + 0.5f * accel * dtSeconds * dtSeconds;
    velocityMmPerSecond_ += accel * dtSeconds;

    // P = F P F' + Q, with F = [[1, dt], [0, 1]].
    const float p00 = p00_ + dtSeconds * (p01_ + p10_) + dtSeconds * dtSeconds * p11_;
    const float p01 = p01_ + dtSeconds * p11_;
    const float p10 = p10_ + dtSeconds * p11_;
    const float p11 = p11_;

    // Continuous white-acceleration process noise, discretized.
    const float dt2 = dtSeconds * dtSeconds;
    const float dt3 = dt2 * dtSeconds;
    const float dt4 = dt3 * dtSeconds;
    const float variance = params_.processNoiseMmPerS2 * params_.processNoiseMmPerS2;

    p00_ = p00 + variance * dt4 * 0.25f;
    p01_ = p01 + variance * dt3 * 0.5f;
    p10_ = p10 + variance * dt3 * 0.5f;
    p11_ = p11 + variance * dt2;
}

bool BallEstimator::correct(float measuredMm) {
    if (!isInitialized_) {
        initializeAt(measuredMm);

        return true;
    }

    const float sigma = sensorSigmaAt(measuredMm);
    const float measurementVariance = sigma * sigma;
    const float innovation = measuredMm - positionMm_;
    const float innovationVariance = p00_ + measurementVariance;

    if (innovationVariance <= 0.0f) {
        initializeAt(measuredMm);

        return true;
    }

    if (fabsf(innovation) > params_.innovationGateSigma * sqrtf(innovationVariance)) {
        consecutiveRejects_++;

        // Several in a row means the prediction has drifted away from reality —
        // the ball was lifted, or nudged — so believe the sensor instead.
        if (consecutiveRejects_ >= Config::kMaxConsecutiveRejects) {
            initializeAt(measuredMm);
        }

        return false;
    }

    consecutiveRejects_ = 0;

    const float gainPosition = p00_ / innovationVariance;
    const float gainVelocity = p10_ / innovationVariance;

    positionMm_ += gainPosition * innovation;
    velocityMmPerSecond_ += gainVelocity * innovation;

    // P = (I - K H) P, with H = [1, 0].
    const float p00 = (1.0f - gainPosition) * p00_;
    const float p01 = (1.0f - gainPosition) * p01_;
    const float p10 = p10_ - gainVelocity * p00_;
    const float p11 = p11_ - gainVelocity * p01_;

    p00_ = p00;
    p01_ = p01;
    p10_ = p10;
    p11_ = p11;

    return true;
}
