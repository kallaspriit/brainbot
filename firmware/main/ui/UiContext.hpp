#pragma once

class Navigator;
class LatencyService;
class MotorService;
class UiState;

/**
 * What screens and components can reach: state to show, motor and latency commands to send, and navigation.
 *
 * Passed by reference; nothing in the UI reaches services any other way.
 */
struct UiContext {
    UiState& state;          // State owned outside the UI, as LVGL subjects
    MotorService& motor;     // Motor commands (thread-safe requests that only queue)
    Navigator& navigator;    // Open screens, back, home
    LatencyService& latency; // Latency measurement commands (D-034)
};
