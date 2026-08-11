#include "diagnostics/SensorNoiseTest.hpp"

#include <math.h>

#include "stream_operators.hpp"

namespace {

/**
 * Population standard deviation from running sums.
 *
 * @param sum        Sum of the samples.
 * @param sumSquares Sum of the squared samples.
 * @param count      Number of samples.
 * @returns The standard deviation, or 0 with fewer than two samples.
 */
double standardDeviation(double sum, double sumSquares, uint32_t count) {
    if (count < 2) {
        return 0.0;
    }

    const double mean = sum / (double)count;
    const double variance = (sumSquares / (double)count) - (mean * mean);

    // Cancellation can push a near-zero variance slightly negative.
    return variance > 0.0 ? sqrt(variance) : 0.0;
}

} // namespace

void SensorNoiseTest::start(unsigned long durationMs, unsigned long nowMs) {
    sum_ = 0.0;
    sumSquares_ = 0.0;
    stepSum_ = 0.0;
    stepSumSquares_ = 0.0;
    validCount_ = 0;
    rejectedCount_ = 0;
    stepCount_ = 0;
    minMm_ = 0;
    maxMm_ = 0;
    minRejectedMm_ = 0;
    maxRejectedMm_ = 0;
    lastDistanceMm_ = 0.0f;
    lastTimestampMs_ = 0;
    firstTimestampMs_ = 0;
    hasPrevious_ = false;

    startMs_ = nowMs;
    durationMs_ = durationMs;
    state_ = State::Running;
}

void SensorNoiseTest::tick(unsigned long nowMs, const BallMeasurement* measurement) {
    if (state_ != State::Running) {
        return;
    }

    if (nowMs - startMs_ >= durationMs_) {
        state_ = State::Complete;

        return;
    }

    if (measurement == nullptr) {
        return;
    }

    if (!measurement->isValid) {
        if (rejectedCount_ == 0 || measurement->distanceMm < minRejectedMm_) {
            minRejectedMm_ = measurement->distanceMm;
        }

        if (rejectedCount_ == 0 || measurement->distanceMm > maxRejectedMm_) {
            maxRejectedMm_ = measurement->distanceMm;
        }

        rejectedCount_++;

        // A rejected reading breaks the chain: the next valid one is further
        // apart in time, so pairing across the gap would understate the step
        // rate and overstate the noise.
        hasPrevious_ = false;

        return;
    }

    const float distanceMm = (float)measurement->distanceMm;

    if (validCount_ == 0) {
        minMm_ = measurement->distanceMm;
        maxMm_ = measurement->distanceMm;
        firstTimestampMs_ = measurement->timestampMs;
    } else {
        if (measurement->distanceMm < minMm_) {
            minMm_ = measurement->distanceMm;
        }

        if (measurement->distanceMm > maxMm_) {
            maxMm_ = measurement->distanceMm;
        }
    }

    sum_ += (double)distanceMm;
    sumSquares_ += (double)distanceMm * (double)distanceMm;
    validCount_++;

    if (hasPrevious_) {
        const double step = (double)distanceMm - (double)lastDistanceMm_;

        stepSum_ += step;
        stepSumSquares_ += step * step;
        stepCount_++;
    }

    lastDistanceMm_ = distanceMm;
    lastTimestampMs_ = measurement->timestampMs;
    hasPrevious_ = true;
}

void SensorNoiseTest::report(Print& out, uint32_t timingBudgetUs) const {
    if (validCount_ < 2) {
        out << "no valid readings - is the sensor connected and the ball in range?" << endl;

        return;
    }

    const uint32_t totalCount = validCount_ + rejectedCount_;
    const float rejectedPercent = 100.0f * (float)rejectedCount_ / (float)totalCount;

    const double mean = sum_ / (double)validCount_;
    const double sigma = standardDeviation(sum_, sumSquares_, validCount_);
    const double stepSigma = standardDeviation(stepSum_, stepSumSquares_, stepCount_);

    const unsigned long spanMs = lastTimestampMs_ - firstTimestampMs_;
    const float rateHz = spanMs > 0 ? (1000.0f * (float)(validCount_ - 1) / (float)spanMs) : 0.0f;

    out << "Sensor noise over " << (float)durationMs_ / 1000.0f << " s, timing budget " << timingBudgetUs << " us" << endl;
    out << "  samples   " << validCount_ << " valid, " << rejectedCount_ << " rejected (" << rejectedPercent << " %), " << rateHz << " Hz" << endl;
    out << "  distance  mean " << (float)mean << " mm, min " << minMm_ << ", max " << maxMm_ << ", sigma " << (float)sigma << " mm" << endl;

    if (rejectedCount_ > 0) {
        out << "  rejected  raw values " << minRejectedMm_ << ".." << maxRejectedMm_ << endl;
    }

    out << "  step      sigma " << (float)stepSigma << " mm between consecutive readings" << endl;

    if (rateHz > 0.0f) {
        // What a raw finite difference would hand a derivative term. Compare it
        // against the ball's real speeds — a few hundred mm/s — to see whether
        // differentiating this signal is meaningful at all.
        out << "  velocity  " << (float)(stepSigma * rateHz) << " mm/s of noise from a raw finite difference" << endl;
    }
}
