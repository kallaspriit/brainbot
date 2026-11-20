#pragma once

#include <cstddef>
#include <cstdint>

/**
 * One latency measurement: from the LED turning on to the screen brightening past the threshold. Only the on edge is
 * measured (D-035): the off edge is waited for but says more about the monitor than the latency.
 */
struct LatencySample {
    uint32_t latencyUs = 0; // LED on to the screen crossing the threshold
};

/**
 * Statistics over a run's samples (Latency measurement plan, D8). Percentiles by nearest rank.
 */
struct LatencyStats {
    uint16_t count = 0;
    uint32_t minUs = 0;
    uint32_t maxUs = 0;
    uint32_t medianUs = 0;
    uint32_t p5Us = 0;
    uint32_t p95Us = 0;
};

/**
 * Snapshot of the latency measurement (D-034), published by LatencyService and copied into UiState by the bridge.
 *
 * Levels are brightness in raw ADC counts, 0-4095, higher is brighter: the DFR0026's output voltage rises with light
 * (measured 2026-11-02).
 */
struct LatencyStatus {
    enum class State : uint8_t {
        IDLE,      // Sensor stopped, LED off
        AIMING,    // Sensor running, live level, LED under manual control; a finished run's result stays shown
        LEARNING,  // First cycle of a run: dark and bright levels, threshold, first estimate
        MEASURING, // Cycles running, samples coming in
    };

    enum class Kind : uint8_t {
        MEASUREMENT, // Through the camera and the screen
        SELF_TEST,   // Sensor pressed against the LED: the console's own chain
    };

    static constexpr size_t MAX_SAMPLES = 200;

    State state = State::IDLE;
    Kind kind = Kind::MEASUREMENT; // Of the run in progress or the last one
    bool isLedOn = false;

    // Live level, refreshed about ten times a second while the sensor runs
    uint16_t level = 0;    // 1 ms average, the latest
    uint16_t levelMin = 0; // Lowest 1 ms average over the last 100 ms
    uint16_t levelMax = 0; // Highest 1 ms average over the last 100 ms

    // Learned in the run's first cycle (plan D5)
    bool hasLevels = false;
    uint16_t darkLevel = 0;
    uint16_t brightLevel = 0;
    uint16_t threshold = 0;        // Midpoint between dark and bright
    uint16_t hysteresis = 0;       // 15 % of the swing
    uint16_t contrastPerMille = 0; // Swing as a share of the ADC range
    uint16_t ripple = 0;           // Range of the 1 ms average while bright: backlight PWM and flicker (plan D6)
    bool isRippleHigh = false;     // Ripple above the hysteresis: detection may trigger on it

    // Samples of the run in progress or the last one
    uint16_t targetCount = 0;
    uint16_t sampleCount = 0;
    uint16_t missCount = 0; // Cycles that timed out, all of the run
    LatencySample samples[MAX_SAMPLES];
    LatencyStats stats;

    // Outcome of the last run
    bool hasResult = false;        // A run ended with samples; stats and samples hold it
    bool isFailed = false;         // The last run failed; message says why
    bool isSelfTestPassed = false; // Kind SELF_TEST with a median under 1 ms
    char message[112] = "";        // What happens now, or how the last run ended

    uint32_t version = 0; // Changes with every new sample, state, result or message; the level doesn't change it
};
