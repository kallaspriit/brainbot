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
