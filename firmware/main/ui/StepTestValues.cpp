#include "StepTestValues.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/components/NumberPad.hpp"

static constexpr int32_t MAX_STEP_DECITURNS = 1000;     // The pad's limit for position steps (100 turns); the service clamps too
static constexpr int32_t MILLITURNS_PER_DECITURN = 100; // Steps are set in 0.1 turn: finer is no use for a step test

namespace {
/**
 * The setting the number pad edits; one pad at a time, so file-wide.
 */
struct PadTarget {
    UiContext* context = nullptr;
    StepTestValue value = StepTestValue::VELOCITY_A;
};

PadTarget padTarget;
} // namespace

bool isStepTestValueShown(UiState::Motor& motor, StepTestValue value) {
    bool isPosition = lv_subject_get_int(&motor.isStepTestPosition) != 0;

    switch (value) {
        case StepTestValue::VELOCITY_A:
        case StepTestValue::VELOCITY_B:
            return !isPosition;

        case StepTestValue::STEP:
        case StepTestValue::MOVE_SPEED:
            return isPosition;

        default:
            return true;
    }
}

void formatStepTestValue(UiState::Motor& motor, StepTestValue value, char* buffer, size_t bufferSize) {
    switch (value) {
        case StepTestValue::VELOCITY_A:
            snprintf(buffer, bufferSize, "%" PRId32 " rpm", lv_subject_get_int(&motor.stepTestVelocityARpm));
            break;

        case StepTestValue::VELOCITY_B:
            snprintf(buffer, bufferSize, "%" PRId32 " rpm", lv_subject_get_int(&motor.stepTestVelocityBRpm));
            break;

        case StepTestValue::STEP: {
            int32_t milliturns = lv_subject_get_int(&motor.stepTestStepMilliturns);
            int32_t deciturns = std::abs(milliturns) / MILLITURNS_PER_DECITURN;
            snprintf(buffer, bufferSize, "%s%" PRId32 ".%" PRId32 " turns", milliturns < 0 ? "-" : "+", deciturns / 10, deciturns % 10);
            break;
        }

        case StepTestValue::MOVE_SPEED:
            snprintf(buffer, bufferSize, "%" PRId32 " rpm", lv_subject_get_int(&motor.stepTestMoveSpeedRpm));
            break;

        case StepTestValue::ACCELERATION:
            snprintf(buffer, bufferSize, "%" PRId32 " rpm/s", lv_subject_get_int(&motor.stepTestAccelerationRpmPerSecond));
            break;

        case StepTestValue::DWELL: {
            int32_t dwellMs = lv_subject_get_int(&motor.stepTestDwellMs);
            snprintf(buffer, bufferSize, "%" PRId32 ".%" PRId32 " s", dwellMs / 1000, dwellMs % 1000 / 100);
            break;
        }
    }
}

static void onValueEntered(int32_t entered, void* userData) {
    PadTarget* target = static_cast<PadTarget*>(userData);
    UiState::Motor& motor = target->context->state.motor;
    MotorService& service = target->context->motor;

    switch (target->value) {
        case StepTestValue::VELOCITY_A:
            service.setStepTestVelocities(entered, lv_subject_get_int(&motor.stepTestVelocityBRpm));
            break;

        case StepTestValue::VELOCITY_B:
            service.setStepTestVelocities(lv_subject_get_int(&motor.stepTestVelocityARpm), entered);
            break;

        case StepTestValue::STEP:
            service.setStepTestStepMilliturns(entered * MILLITURNS_PER_DECITURN);
            break;

        case StepTestValue::MOVE_SPEED:
            service.setStepTestMoveSpeedRpm(entered);
            break;

        case StepTestValue::ACCELERATION:
            service.setStepTestAccelerationRpmPerSecond(entered);
            break;

        case StepTestValue::DWELL:
            // The pad works in tenths of a second
            service.setStepTestDwellMs(entered * 100);
            break;
    }
}

void openStepTestValueEditor(UiContext& context, StepTestValue value) {
    UiState::Motor& motor = context.state.motor;
    int32_t limitRpm = lv_subject_get_int(&motor.velocityLimitRpm);

    padTarget.context = &context;
    padTarget.value = value;

    NumberPadOptions options;
    options.onConfirm = onValueEntered;
    options.userData = &padTarget;

    switch (value) {
        case StepTestValue::VELOCITY_A:
        case StepTestValue::VELOCITY_B:
            options.title = value == StepTestValue::VELOCITY_A ? "Step test: speed A" : "Step test: speed B";
            options.unit = "rpm";
            options.value = lv_subject_get_int(value == StepTestValue::VELOCITY_A ? &motor.stepTestVelocityARpm : &motor.stepTestVelocityBRpm);
            options.min = -limitRpm;
            options.max = limitRpm;
            break;

        case StepTestValue::STEP:
            options.title = "Step test: distance there and back";
            options.unit = "turns";
            options.value = lv_subject_get_int(&motor.stepTestStepMilliturns) / MILLITURNS_PER_DECITURN;
            options.min = -MAX_STEP_DECITURNS;
            options.max = MAX_STEP_DECITURNS;
            options.decimals = 1;
            break;

        case StepTestValue::MOVE_SPEED:
            options.title = "Step test: move speed";
            options.unit = "rpm";
            options.value = lv_subject_get_int(&motor.stepTestMoveSpeedRpm);
            options.min = 1;
            options.max = limitRpm;
            break;

        case StepTestValue::ACCELERATION:
            options.title = "Step test: ramp (raises the drive's ramp limits for the test)";
            options.unit = "rpm/s";
            options.value = lv_subject_get_int(&motor.stepTestAccelerationRpmPerSecond);
            options.min = 10;    // MotorService::MIN_ACCELERATION_RPM_PER_SECOND
            options.max = 30000; // MotorService::MAX_STEP_TEST_ACCELERATION_RPM_PER_SECOND
            break;

        case StepTestValue::DWELL:
            options.title = "Step test: time at each target";
            options.unit = "s";
            options.value = lv_subject_get_int(&motor.stepTestDwellMs) / 100;
            options.min = 5;
            options.max = 100;
            options.decimals = 1;
            break;
    }

    openNumberPad(context.navigator.getOverlayLayer(), options);
}
