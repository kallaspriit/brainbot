#pragma once

#include <Arduino.h>

#include "BallBeamSystem.hpp"
#include "Config.hpp"

/**
 * Streams telemetry frames at a steady rate from core 0.
 *
 * Runs on the console's core rather than the control loop's on purpose: framing
 * and printing a frame costs far more than the control tick, and a host that
 * stops reading USB must never be able to stall the beam.
 */
class TelemetryStream {
  public:
    /**
     * @param out    Stream to emit frames on.
     * @param system Source of the state being emitted.
     */
    TelemetryStream(Print& out, BallBeamSystem& system)
        : out_(out),
          system_(system) {
    }

    /** Emits a frame if one is due. Call from the core 0 loop. */
    void loop();

    /**
     * @param isEnabled Whether frames should stream.
     */
    void setEnabled(bool isEnabled) {
        isEnabled_ = isEnabled;
    }

    /** @returns true while frames are streaming. */
    bool isEnabled() const {
        return isEnabled_;
    }

    /**
     * Sets the frame period. The default ~33 Hz is fine for watching the ball
     * but aliases the servo's step response, which settles within a sample or
     * two — raise it before a characterization run or the transient shape is an
     * artefact of the sample rate.
     *
     * @param intervalMs Period in milliseconds, clamped to the configured range.
     */
    void setIntervalMs(unsigned long intervalMs);

    /** @returns the frame period in milliseconds. */
    unsigned long intervalMs() const {
        return intervalMs_;
    }

  private:
    Print& out_;
    BallBeamSystem& system_;
    bool isEnabled_ = false;
    unsigned long intervalMs_ = Config::kDefaultDebugIntervalMs;
    unsigned long lastFrameMs_ = 0;
};
