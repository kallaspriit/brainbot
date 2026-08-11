#include "console/RigCommands.hpp"

#include <Arduino.h>
#include <cstring>

#include "Config.hpp"
#include "hardware/HardwareLock.hpp"
#include "stream_operators.hpp"

namespace {

/**
 * Parses an "on"/"off" (or "1"/"0") argument into a flag.
 *
 * @param console Console to pull the token from.
 * @param target  Flag written when the token is recognised.
 * @returns True when a valid token was given, false otherwise.
 */
bool parseOnOff(SerialConsole& console, bool& target) {
    const char* token = console.nextToken();

    if (token == nullptr) {
        return false;
    }

    if (strcmp(token, "on") == 0 || strcmp(token, "1") == 0) {
        target = true;

        return true;
    }

    if (strcmp(token, "off") == 0 || strcmp(token, "0") == 0) {
        target = false;

        return true;
    }

    return false;
}

} // namespace

void RigCommands::registerCommands(SerialConsole& console) {
    console.addCommand("debug", "debug <on|off>             - stream telemetry frames for Serial Studio", [this](SerialConsole& c) {
        bool isEnabled = telemetry_.isEnabled();

        if (!parseOnOff(c, isEnabled)) {
            Serial << "usage: debug <on|off>  (currently " << (telemetry_.isEnabled() ? "on" : "off") << ")" << endl;

            return;
        }

        telemetry_.setEnabled(isEnabled);

        Serial << "debug " << (isEnabled ? "on" : "off") << " at " << (1000.0f / (float)telemetry_.intervalMs()) << " Hz" << endl;

        if (isEnabled && !system_.sensor().isPresent()) {
            Serial << "note: no distance sensor detected, distance stays 0" << endl;
        }
    });

    console.addCommand("debugrate", "debugrate <hz>             - telemetry frame rate (raise before step tests)", [this](SerialConsole& c) {
        const float hz = c.nextFloat(0.0f);

        if (hz <= 0.0f) {
            Serial << "usage: debugrate <hz>  (currently " << (1000.0f / (float)telemetry_.intervalMs()) << " Hz)" << endl;

            return;
        }

        telemetry_.setIntervalMs((unsigned long)(1000.0f / hz));

        Serial << "debug rate " << (1000.0f / (float)telemetry_.intervalMs()) << " Hz (" << telemetry_.intervalMs() << " ms)" << endl;
    });

    console.addCommand("angle", "angle <deg>                - command the beam angle directly", [this](SerialConsole& c) {
        const float degrees = c.nextFloat(0.0f);

        float commanded = 0.0f;

        {
            const HardwareLock lock;

            // Stop any running test, otherwise the next tick overwrites this.
            system_.motionTest().stop();
            system_.beam().enable();
            system_.beam().setAngle(degrees);

            commanded = system_.beam().targetAngle();
        }

        Serial << "angle " << commanded << " deg (requested " << degrees << ")" << endl;
    });

    console.addCommand("trim", "trim <deg>                 - servo angle at which the beam is level", [this](SerialConsole& c) {
        const float degrees = c.nextFloat(system_.beam().trim());

        {
            const HardwareLock lock;

            system_.beam().setTrim(degrees);
        }

        Serial << "trim " << degrees << " deg" << endl;
    });

    console.addCommand("target", "target <mm>                - where the ball should be held", [this](SerialConsole& c) {
        const float positionMm = c.nextFloat(-1.0f);

        if (positionMm < 0.0f) {
            Serial << "usage: target <mm>  (currently " << system_.target() << " mm)" << endl;

            return;
        }

        system_.setTarget(positionMm);

        Serial << "target " << positionMm << " mm" << endl;
    });

    console.addCommand("tilt", "tilt <on|off>              - rock the beam +-4 deg every 3 s", [this](SerialConsole& c) {
        bool isEnabled = false;

        if (!parseOnOff(c, isEnabled)) {
            Serial << "usage: tilt <on|off>  (currently " << (system_.motionTest().isRunning() ? "on" : "off") << ")" << endl;

            return;
        }

        {
            const HardwareLock lock;

            if (isEnabled) {
                system_.beam().enable();
                system_.motionTest().startSquare(4.0f, 3000, millis());
            } else {
                system_.motionTest().stop();
                system_.beam().hold();
            }
        }

        Serial << "tilt " << (isEnabled ? "on" : "off") << endl;
    });

    console.addCommand("steptest", "steptest <deg> [holdMs]    - square wave; run small (0.2-1 deg) too", [this](SerialConsole& c) {
        const float amplitude = c.nextFloat(0.0f);
        const int holdMs = c.nextInt(1000);

        if (amplitude <= 0.0f || holdMs <= 0) {
            Serial << "usage: steptest <deg> [holdMs]" << endl;

            return;
        }

        {
            const HardwareLock lock;

            system_.beam().enable();
            system_.motionTest().startSquare(amplitude, (unsigned long)holdMs, millis());
        }

        Serial << "steptest +-" << amplitude << " deg, " << holdMs << " ms hold" << endl;

        if (telemetry_.intervalMs() > 10) {
            Serial << "note: raise debugrate (>=100 Hz) or the transient is aliased" << endl;
        }
    });

    console.addCommand("sinetest", "sinetest <deg> <hz>        - sine sweep; crossover is near 0.3 Hz", [this](SerialConsole& c) {
        const float amplitude = c.nextFloat(0.0f);
        const float hz = c.nextFloat(0.0f);

        if (amplitude <= 0.0f || hz <= 0.0f) {
            Serial << "usage: sinetest <deg> <hz>" << endl;

            return;
        }

        {
            const HardwareLock lock;

            system_.beam().enable();
            system_.motionTest().startSine(amplitude, hz, millis());
        }

        Serial << "sinetest +-" << amplitude << " deg at " << hz << " Hz" << endl;
    });

    console.addCommand("teststop", "teststop                   - stop the running motion test", [this](SerialConsole&) {
        {
            const HardwareLock lock;

            system_.motionTest().stop();
            system_.beam().hold();
        }

        Serial << "test stopped" << endl;
    });

    console.addCommand("noisetest", "noisetest [seconds]        - sensor noise with the ball held still", [this](SerialConsole& c) {
        const float seconds = c.nextFloat(5.0f);

        if (seconds <= 0.0f || seconds > 60.0f) {
            Serial << "usage: noisetest [seconds 0..60]" << endl;

            return;
        }

        if (!system_.sensor().isPresent()) {
            Serial << "no distance sensor detected" << endl;

            return;
        }

        const bool wasStreaming = telemetry_.isEnabled();

        // Frames would interleave with the report and confuse Serial Studio's
        // frame reader, and the console is blocked for the run anyway.
        telemetry_.setEnabled(false);

        {
            const HardwareLock lock;

            system_.noiseTest().start((unsigned long)(seconds * 1000.0f), millis());
        }

        Serial << "sampling for " << seconds << " s - keep the ball still" << endl;

        // Blocking is fine here: the control tick lives on core 1 and keeps
        // running, so the beam stays under control while the console waits. The
        // deadline is a backstop in case core 1 is not ticking at all, which
        // would otherwise wedge the console with no way back.
        const unsigned long deadlineMs = millis() + (unsigned long)(seconds * 1000.0f) + 2000;

        while (!system_.noiseTest().isComplete() && millis() < deadlineMs) {
            delay(20);
        }

        if (!system_.noiseTest().isComplete()) {
            Serial << "timed out - is the control loop running on core 1?" << endl;

            return;
        }

        uint32_t budgetUs = 0;

        {
            const HardwareLock lock;

            budgetUs = system_.sensor().timingBudgetUs();
        }

        system_.noiseTest().report(Serial, budgetUs);

        telemetry_.setEnabled(wasStreaming);
    });

    console.addCommand("budget", "budget [us]                - sensor integration time (omit to read)", [this](SerialConsole& c) {
        const int budgetUs = c.nextInt(-1);

        if (budgetUs < 0) {
            uint32_t current = 0;

            {
                const HardwareLock lock;

                current = system_.sensor().timingBudgetUs();
            }

            Serial << "timing budget " << current << " us" << endl;

            return;
        }

        if (budgetUs < 20000 || budgetUs > 500000) {
            Serial << "usage: budget [us 20000..500000]" << endl;

            return;
        }

        bool isOk = false;
        uint32_t actual = 0;

        {
            const HardwareLock lock;

            isOk = system_.sensor().setTimingBudgetUs((uint32_t)budgetUs);
            actual = system_.sensor().timingBudgetUs();
        }

        if (!isOk) {
            Serial << "sensor rejected that timing budget" << endl;

            return;
        }

        // The device quantizes the budget to what its sequence-step timeouts can
        // express, so the value it reports back is the one that matters.
        Serial << "timing budget " << actual << " us (requested " << budgetUs << ")" << endl;
    });

    console.addCommand("sensorreset", "sensorreset                - re-initialize the distance sensor", [this](SerialConsole&) {
        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = sensor_.begin();
        }

        if (isOk) {
            Serial << "sensor re-initialized" << endl;
        } else {
            Serial << "sensor did not respond - power cycle it" << endl;
        }
    });

    console.addCommand("sensor", "sensor <default|long|fast|accurate> - VL53L0X preset profile", [this](SerialConsole& c) {
        const char* token = c.nextToken();

        if (token == nullptr) {
            Serial << "usage: sensor <default|long|fast|accurate>" << endl;

            return;
        }

        Vl53l0xBallSensor::Profile profile = Vl53l0xBallSensor::Profile::Default;

        if (strcmp(token, "long") == 0) {
            profile = Vl53l0xBallSensor::Profile::LongRange;
        } else if (strcmp(token, "fast") == 0) {
            profile = Vl53l0xBallSensor::Profile::HighSpeed;
        } else if (strcmp(token, "accurate") == 0) {
            profile = Vl53l0xBallSensor::Profile::HighAccuracy;
        } else if (strcmp(token, "default") != 0) {
            Serial << "usage: sensor <default|long|fast|accurate>" << endl;

            return;
        }

        bool isOk = false;

        {
            const HardwareLock lock;

            isOk = sensor_.setProfile(profile);
        }

        uint32_t actual = 0;

        {
            const HardwareLock lock;

            actual = system_.sensor().timingBudgetUs();
        }

        if (isOk) {
            Serial << "sensor profile " << token << ", timing budget " << actual << " us" << endl;
        } else {
            Serial << "sensor rejected that profile" << endl;
        }
    });

    console.addCommand("rolltest", "rolltest <deg> [ms]        - measure ball accel per degree of tilt", [this](SerialConsole& c) {
        const float degrees = c.nextFloat(0.0f);
        const int durationMs = c.nextInt(1500);

        if (degrees == 0.0f || durationMs < 300) {
            Serial << "usage: rolltest <deg> [ms >= 300]  (put the ball at the uphill end first)" << endl;

            return;
        }

        if (!system_.sensor().isPresent()) {
            Serial << "no distance sensor detected" << endl;

            return;
        }

        const bool wasStreaming = telemetry_.isEnabled();

        telemetry_.setEnabled(false);

        {
            const HardwareLock lock;

            system_.beam().enable();
            system_.motionTest().stop();
            system_.rollTest().start(degrees, (unsigned long)durationMs, 250, millis());
        }

        Serial << "rolling at " << degrees << " deg for " << durationMs << " ms" << endl;

        const unsigned long deadlineMs = millis() + (unsigned long)durationMs + 2000;

        while (!system_.rollTest().isComplete() && millis() < deadlineMs) {
            delay(20);
        }

        {
            const HardwareLock lock;

            system_.rollTest().stop();
            system_.beam().setAngle(0.0f);
        }

        if (!system_.rollTest().isComplete()) {
            Serial << "timed out - is the control loop running on core 1?" << endl;
        } else {
            system_.rollTest().report(Serial);
        }

        telemetry_.setEnabled(wasStreaming);
    });

    console.addCommand("est", "est [accel] [q] [gate]     - estimator tuning (omit to read)", [this](SerialConsole& c) {
        const float accelPerDegree = c.nextFloat(0.0f);

        if (accelPerDegree == 0.0f) {
            const BallEstimator::Params& params = system_.estimator().params();
            const BallBeamState state = system_.state();

            Serial << "accel per degree  " << params.accelPerDegree << " mm/s^2/deg" << endl;
            Serial << "process noise     " << params.processNoiseMmPerS2 << " mm/s^2" << endl;
            Serial << "sensor sigma      " << params.sensorNoiseBaseMm << " + " << params.sensorNoiseQuadratic << " * d^2  (" << system_.estimator().sensorSigmaAt(state.positionMm) << " mm here)" << endl;
            Serial << "innovation gate   " << params.innovationGateSigma << " sigma, " << system_.estimator().consecutiveRejects() << " rejected in a row" << endl;
            Serial << "estimate          " << state.positionMm << " +-" << system_.estimator().positionSigmaMm() << " mm, " << state.velocityMmPerSecond << " +-"
                   << system_.estimator().velocitySigmaMmPerS() << " mm/s" << endl;

            return;
        }

        const float processNoise = c.nextFloat(system_.estimator().params().processNoiseMmPerS2);
        const float gate = c.nextFloat(system_.estimator().params().innovationGateSigma);

        {
            const HardwareLock lock;

            BallEstimator::Params& params = system_.estimator().params();

            params.accelPerDegree = accelPerDegree;
            params.processNoiseMmPerS2 = processNoise;
            params.innovationGateSigma = gate;
        }

        Serial << "accel " << accelPerDegree << " mm/s^2/deg, process noise " << processNoise << " mm/s^2, gate " << gate << " sigma" << endl;
    });

    console.addCommand("state", "state                      - ball position, velocity, beam angle", [this](SerialConsole&) {
        const BallBeamState state = system_.state();

        Serial << "ball " << state.positionMm << " mm, velocity " << state.velocityMmPerSecond << " mm/s, target " << state.targetMm << " mm" << endl;
        Serial << "beam " << state.beamAngleDegrees << " deg (commanded " << system_.beam().targetAngle() << "), trim " << system_.beam().trim() << endl;
        Serial << "ball position " << (state.isValid ? "valid" : "UNKNOWN") << ", sensor " << system_.sensor().name() << (system_.sensor().isPresent() ? " present" : " MISSING") << endl;
        Serial << "test: ";
        system_.motionTest().describe(Serial);
        Serial << "controller: " << (system_.controller() != nullptr ? system_.controller()->name() : "none (open loop)") << endl;
    });
}
