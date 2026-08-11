#include "diagnostics/BallRollTest.hpp"

#include <math.h>

#include "stream_operators.hpp"

void BallRollTest::start(float angleDegrees, unsigned long durationMs, unsigned long settleMs, unsigned long nowMs) {
    angleDegrees_ = angleDegrees;
    startMs_ = nowMs;
    durationMs_ = durationMs;
    settleMs_ = settleMs;

    sumT_ = 0.0;
    sumT2_ = 0.0;
    sumT3_ = 0.0;
    sumT4_ = 0.0;
    sumY_ = 0.0;
    sumTy_ = 0.0;
    sumT2y_ = 0.0;
    sampleCount_ = 0;
    rejectedCount_ = 0;
    firstMm_ = 0.0f;
    lastMm_ = 0.0f;

    state_ = State::Running;
}

void BallRollTest::stop() {
    state_ = State::Idle;
}

void BallRollTest::tick(unsigned long nowMs, const BallMeasurement* measurement) {
    if (state_ != State::Running) {
        return;
    }

    const unsigned long elapsedMs = nowMs - startMs_;

    if (elapsedMs >= durationMs_) {
        state_ = State::Complete;

        return;
    }

    if (measurement == nullptr || elapsedMs < settleMs_) {
        return;
    }

    if (!measurement->isValid) {
        rejectedCount_++;

        return;
    }

    // Time measured from the end of the settle window, so the fitted constant
    // term stays near the ball's position rather than extrapolating backwards.
    const double t = (double)(elapsedMs - settleMs_) / 1000.0;
    const double y = (double)measurement->distanceMm;
    const double t2 = t * t;

    sumT_ += t;
    sumT2_ += t2;
    sumT3_ += t2 * t;
    sumT4_ += t2 * t2;
    sumY_ += y;
    sumTy_ += t * y;
    sumT2y_ += t2 * y;

    if (sampleCount_ == 0) {
        firstMm_ = (float)measurement->distanceMm;
    }

    lastMm_ = (float)measurement->distanceMm;
    sampleCount_++;
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

    const double c2 = detC2 / determinant;
    const double c1 = detC1 / determinant;

    const double accel = 2.0 * c2;

    out << "Roll test at " << angleDegrees_ << " deg, " << sampleCount_ << " samples, " << rejectedCount_ << " rejected" << endl;
    out << "  travel    " << firstMm_ << " -> " << lastMm_ << " mm" << endl;
    out << "  initial   " << (float)c1 << " mm/s" << endl;
    out << "  accel     " << (float)accel << " mm/s^2" << endl;

    if (fabsf(angleDegrees_) > 0.01f) {
        const double perDegree = accel / (double)angleDegrees_;

        out << "  constant  " << (float)perDegree << " mm/s^2 per degree" << endl;
        out << "  -> set kAccelPerDegree in Config.hpp to this value" << endl;
    }
}
