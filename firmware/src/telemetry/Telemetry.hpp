#pragma once

#include <Arduino.h>

#include "St3215.hpp"

/**
 * One tick's worth of state, captured on core 1 and printed on core 0.
 *
 * Copied out under the hardware lock and printed outside it, so a host that
 * stops draining USB blocks the console rather than the control loop.
 */
struct TelemetrySnapshot {
    uint16_t distanceMm = 0;
    float beamAngleDegrees = 0.0f;
    float targetAngleDegrees = 0.0f;
    St3215::ServoFeedback feedback;
};

namespace Telemetry {

/**
 * Emits one frame for Serial Studio, in the delimited CSV format its default
 * parser expects: a "$" start delimiter, comma-separated values in the order the
 * project file's dataset indexes reference, and a ";" end delimiter. Console
 * text outside those delimiters is ignored by the frame reader, so interactive
 * commands keep working while frames stream.
 *
 * Fields: distance, beam angle, target angle, position, speed, load, current,
 * voltage, temperature. The order is load-bearing — it matches the dataset
 * indexes in serial-studio/brainbot.ssproj, so new fields go on the end.
 *
 * @param out      Stream to write the frame to.
 * @param snapshot State to emit.
 */
void printFrame(Print& out, const TelemetrySnapshot& snapshot);

} // namespace Telemetry
