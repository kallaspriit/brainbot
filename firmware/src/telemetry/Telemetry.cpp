#include "telemetry/Telemetry.hpp"

#include "stream_operators.hpp"

void Telemetry::printFrame(Print& out, const TelemetrySnapshot& snapshot) {
    out << "$" << snapshot.distanceMm << "," << snapshot.beamAngleDegrees << "," << snapshot.targetAngleDegrees << "," << snapshot.feedback.position << "," << snapshot.feedback.speed << ","
        << snapshot.feedback.load << "," << snapshot.feedback.currentMa << "," << (snapshot.feedback.voltageDeciV / 10.0f) << "," << snapshot.feedback.temperatureC << ","
        << snapshot.estimatedPositionMm << "," << snapshot.estimatedVelocityMmPerSecond << ";" << endl;
}
