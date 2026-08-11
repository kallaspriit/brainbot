#include "telemetry/TelemetryStream.hpp"

#include "Config.hpp"
#include "telemetry/Telemetry.hpp"

void TelemetryStream::setIntervalMs(unsigned long intervalMs) {
    if (intervalMs < Config::kMinDebugIntervalMs) {
        intervalMs = Config::kMinDebugIntervalMs;
    }

    if (intervalMs > Config::kMaxDebugIntervalMs) {
        intervalMs = Config::kMaxDebugIntervalMs;
    }

    intervalMs_ = intervalMs;
}

void TelemetryStream::loop() {
    if (!isEnabled_) {
        return;
    }

    const unsigned long nowMs = millis();

    if (nowMs - lastFrameMs_ < intervalMs_) {
        return;
    }

    lastFrameMs_ = nowMs;

    TelemetrySnapshot snapshot;

    // Copy under the lock, print outside it.
    system_.snapshot(snapshot);

    Telemetry::printFrame(out_, snapshot);
}
