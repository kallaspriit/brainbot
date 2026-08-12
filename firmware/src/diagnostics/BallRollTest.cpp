#include "diagnostics/BallRollTest.hpp"

#include <math.h>

#include "Config.hpp"
#include "stream_operators.hpp"

void BallRollTest::start(float angleDegrees, unsigned long durationMs, unsigned long nowMs) {
    angleDegrees_ = angleDegrees;
    startMs_ = nowMs;
    durationMs_ = durationMs;

    referenceMm_ = 0.0f;
    hasReference_ = false;
    isTriggered_ = false;
    triggerTimestampMs_ = 0;
    triggerDelayMs_ = 0;

    sumT_ = 0.0;
    sumT2_ = 0.0;
    sumT3_ = 0.0;
    sumT4_ = 0.0;
    sumY_ = 0.0;
    sumY2_ = 0.0;
    sumTy_ = 0.0;
    sumT2y_ = 0.0;
    sampleCount_ = 0;
    rejectedCount_ = 0;
    firstMm_ = 0.0f;
    lastMm_ = 0.0f;
    storedCount_ = 0;

    state_ = State::Running;
}

void BallRollTest::stop() {
    // Abandons a run in progress but never discards a finished one, so levelling
    // the beam after a run cannot throw away the result it was measuring.
    if (state_ == State::Running) {
        state_ = State::Idle;
    }
}

void BallRollTest::tick(unsigned long nowMs, const BallMeasurement* measurement) {
    if (state_ != State::Running) {
        return;
    }

    const unsigned long elapsedMs = nowMs - startMs_;

    // The duration is counted from when the ball starts moving, not from when the
    // command ran. Breakaway takes half a second or so, and charging that to the
    // recording window leaves most of a run spent watching a stationary ball.
    if (isTriggered_) {
        if (elapsedMs - triggerDelayMs_ >= durationMs_) {
            state_ = State::Complete;

            return;
        }
    } else if (elapsedMs >= Config::kRollTriggerWaitMs) {
        state_ = State::Complete;

        return;
    }

    if (measurement == nullptr) {
        return;
    }

    if (!measurement->isValid) {
        rejectedCount_++;

        return;
    }

    if (!hasReference_) {
        referenceMm_ = (float)measurement->distanceMm;
        hasReference_ = true;

        return;
    }

    // Nothing is recorded until the ball has demonstrably moved. Before that the
    // readings may be pinned at the sensor's near-range floor, and fitting a flat
    // stretch is what destroys the acceleration estimate.
    if (!isTriggered_) {
        if (fabsf((float)measurement->distanceMm - referenceMm_) < Config::kRollTriggerMm) {
            return;
        }

        isTriggered_ = true;
        triggerTimestampMs_ = measurement->timestampMs;
        triggerDelayMs_ = elapsedMs;
    }

    // Beyond the gate the sensor is no longer measuring the ball, so the run ends
    // here rather than following it into data that would corrupt the fit.
    if ((float)measurement->distanceMm > Config::kRollMaxFitMm) {
        state_ = State::Complete;

        return;
    }

    // Timed from the sensor's own timestamps rather than the tick clock, so
    // jitter between the two does not enter the fit.
    const double t = (double)(measurement->timestampMs - triggerTimestampMs_) / 1000.0;
    const double y = (double)measurement->distanceMm;
    const double t2 = t * t;

    sumT_ += t;
    sumT2_ += t2;
    sumT3_ += t2 * t;
    sumT4_ += t2 * t2;
    sumY_ += y;
    sumY2_ += y * y;
    sumTy_ += t * y;
    sumT2y_ += t2 * y;

    if (sampleCount_ == 0) {
        firstMm_ = (float)measurement->distanceMm;
    }

    lastMm_ = (float)measurement->distanceMm;
    sampleCount_++;

    if (storedCount_ < kMaxSamples) {
        sampleTimeMs_[storedCount_] = (uint16_t)(measurement->timestampMs - triggerTimestampMs_);
        sampleDistanceMm_[storedCount_] = measurement->distanceMm;
        storedCount_++;
    }
}

void BallRollTest::dumpSamples(Print& out) const {
    out << "t_ms,distance_mm" << endl;

    for (size_t i = 0; i < storedCount_; i++) {
        out << sampleTimeMs_[i] << "," << sampleDistanceMm_[i] << endl;
    }
}

void BallRollTest::report(Print& out) const {
    if (sampleCount_ < 8) {
        out << "only " << sampleCount_ << " valid samples - not enough to fit" << endl;

        return;
    }

    // Normal equations for y = c0 + c1 t + c2 t^2.
    const double n = (double)sampleCount_;
    const double a[3][3] = {
        {n, sumT_, sumT2_},
        {sumT_, sumT2_, sumT3_},
        {sumT2_, sumT3_, sumT4_},
    };
    const double b[3] = {sumY_, sumTy_, sumT2y_};

    const double determinant = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);

    if (fabs(determinant) < 1e-9) {
        out << "fit failed - the ball may not have moved" << endl;

        return;
    }

    // Cramer's rule for the quadratic coefficient and the linear one.
    const double detC2 = a[0][0] * (a[1][1] * b[2] - b[1] * a[2][1]) - a[0][1] * (a[1][0] * b[2] - b[1] * a[2][0]) + b[0] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    const double detC1 = a[0][0] * (b[1] * a[2][2] - a[1][2] * b[2]) - b[0] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * b[2] - b[1] * a[2][0]);

    const double detC0 = b[0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (b[1] * a[2][2] - a[1][2] * b[2]) + a[0][2] * (b[1] * a[2][1] - a[1][1] * b[2]);

    const double c2 = detC2 / determinant;
    const double c1 = detC1 / determinant;
    const double c0 = detC0 / determinant;

    const double accel = 2.0 * c2;
    const double travel = (double)lastMm_ - (double)firstMm_;

    // Residual spread of the samples about the fitted parabola. Comparable to the
    // sensor noise from "noisetest" — much larger means the ball was not doing
    // what the model says, so the acceleration is not to be trusted.
    const double residualSumSquares = sumY2_ - (c0 * sumY_ + c1 * sumTy_ + c2 * sumT2y_);
    const double residualRms = residualSumSquares > 0.0 ? sqrt(residualSumSquares / n) : 0.0;

    // Standard error of the quadratic coefficient for samples spread over a span
    // T, which is what says whether a short arc has actually measured anything.
    const double span = sumT4_ > 0.0 ? sqrt(sqrt(sumT4_ / n)) : 0.0;
    const double accelSigma = span > 0.0 ? 2.0 * residualRms * sqrt(180.0 / n) / (span * span) : 0.0;

    out << "Roll test at " << angleDegrees_ << " deg, " << sampleCount_ << " samples, " << rejectedCount_ << " rejected" << endl;
    out << "  released  " << referenceMm_ << " mm, moved after " << triggerDelayMs_ << " ms" << endl;
    out << "  travel    " << firstMm_ << " -> " << lastMm_ << " mm (" << (float)travel << " mm)" << endl;
    out << "  initial   " << (float)c1 << " mm/s" << endl;
    out << "  residual  " << (float)residualRms << " mm rms about the fit" << endl;
    out << "  accel     " << (float)accel << " +-" << (float)accelSigma << " mm/s^2" << endl;

    if (fabsf(angleDegrees_) > 0.01f) {
        const double perDegree = accel / (double)angleDegrees_;

        out << "  constant  " << (float)perDegree << " +-" << (float)(accelSigma / fabs((double)angleDegrees_)) << " mm/s^2 per degree" << endl;
    }

    if (fabs(travel) < 150.0) {
        out << "  note: short arc - a bigger angle or a longer run tightens this a lot" << endl;
    }
}
