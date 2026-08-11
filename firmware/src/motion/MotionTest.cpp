#include "motion/MotionTest.hpp"

#include <math.h>

#include "stream_operators.hpp"

void MotionTest::startSquare(float amplitudeDegrees, unsigned long holdMs, unsigned long nowMs) {
    mode_ = Mode::Square;
    amplitudeDegrees_ = amplitudeDegrees;
    holdMs_ = holdMs > 0 ? holdMs : 1;
    startMs_ = nowMs;
}

void MotionTest::startSine(float amplitudeDegrees, float frequencyHz, unsigned long nowMs) {
    mode_ = Mode::Sine;
    amplitudeDegrees_ = amplitudeDegrees;
    frequencyHz_ = frequencyHz;
    startMs_ = nowMs;
}

void MotionTest::stop() {
    mode_ = Mode::Off;
}

float MotionTest::angleAt(unsigned long nowMs) const {
    const unsigned long elapsedMs = nowMs - startMs_;

    switch (mode_) {
        case Mode::Square: {
            const unsigned long halfCycle = elapsedMs / holdMs_;

            return (halfCycle % 2 == 0) ? amplitudeDegrees_ : -amplitudeDegrees_;
        }

        case Mode::Sine: {
            const float seconds = (float)elapsedMs / 1000.0f;

            return amplitudeDegrees_ * sinf(2.0f * (float)PI * frequencyHz_ * seconds);
        }

        case Mode::Off:
        default:
            return 0.0f;
    }
}

void MotionTest::describe(Print& out) const {
    switch (mode_) {
        case Mode::Square:
            out << "square +-" << amplitudeDegrees_ << " deg, " << holdMs_ << " ms hold" << endl;

            break;

        case Mode::Sine:
            out << "sine +-" << amplitudeDegrees_ << " deg at " << frequencyHz_ << " Hz" << endl;

            break;

        case Mode::Off:
        default:
            out << "off" << endl;

            break;
    }
}
