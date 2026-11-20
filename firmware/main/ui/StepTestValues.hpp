#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/UiContext.hpp"
#include "ui/UiState.hpp"

/**
 * A step test setting (PAR-010), shown in the Tuning list's Step test section.
 */
enum class StepTestValue : uint8_t {
    VELOCITY_A,   // Velocity steps: first speed
    VELOCITY_B,   // Velocity steps: second speed
    STEP,         // Position steps: distance there and back
    MOVE_SPEED,   // Position steps: speed
    ACCELERATION, // Both: ramp
    DWELL,        // Both: time at each target
};

/**
 * Tells whether a setting applies to the chosen kind of step.
 *
 * @param motor Motor state
 * @param value The setting
 * @return True if it is shown
 */
bool isStepTestValueShown(UiState::Motor& motor, StepTestValue value);

/**
 * Formats a setting with its unit.
 *
 * @param motor Motor state
 * @param value The setting
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
void formatStepTestValue(UiState::Motor& motor, StepTestValue value, char* buffer, size_t bufferSize);

/**
 * Opens the number pad for a setting; OK sets it in the motor service, and it applies from the next step.
 *
 * @param context Motor service, state and navigation; outlives the pad
 * @param value The setting
 */
void openStepTestValueEditor(UiContext& context, StepTestValue value);
