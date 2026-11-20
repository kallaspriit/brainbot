#include "TuningTab.hpp"

#include "services/MotorService.hpp"
#include "services/MotorStatus.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/PositionChart.hpp"
#include "ui/components/SegmentedToggle.hpp"
#include "ui/components/SetupList.hpp"
#include "ui/components/TorqueChart.hpp"
#include "ui/components/VelocityChart.hpp"
#include "ui/fonts/Icons.hpp"

static constexpr int32_t CHART_PERCENT = 50; // The chart's share of the width; the list gets the rest

// Chart

/**
 * Tells which chart fits the motion: position or torque while that mode runs, velocity otherwise.
 *
 * @param motor Motor state
 * @return The running mode's chart, VELOCITY when nothing runs
 */
static MotionMode getChartMode(UiState::Motor& motor) {
    MotionMode mode = static_cast<MotionMode>(lv_subject_get_int(&motor.motionMode));
    bool isRunning = lv_subject_get_int(&motor.isRunning) != 0;

    if (isRunning && (mode == MotionMode::POSITION || mode == MotionMode::TORQUE)) {
        return mode;
    }

    return MotionMode::VELOCITY;
}

static void onMotionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = *static_cast<UiContext*>(lv_observer_get_user_data(observer));
    lv_obj_t* holder = lv_observer_get_target_obj(observer);
    MotionMode mode = getChartMode(context.state.motor);

    // Only when the kind changes: a new chart starts empty. The holder keeps the shown mode + 1, so 0 is "none yet"
    uintptr_t shown = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(holder));
    uintptr_t wanted = static_cast<uintptr_t>(mode) + 1;

    if (shown == wanted) {
        return;
    }

    lv_obj_clean(holder);
    lv_obj_t* chart = nullptr;

    if (mode == MotionMode::POSITION) {
        chart = createPositionChart(holder, context);
    } else if (mode == MotionMode::TORQUE) {
        chart = createTorqueChart(holder, context);
    } else {
        chart = createVelocityChart(holder, context);
    }

    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_user_data(holder, reinterpret_cast<void*>(wanted));
}

// Step test controls (PAR-010)

static void onModeChanged(int32_t index, void* userData) {
    static_cast<UiContext*>(userData)->motor.setStepTestPosition(index == 1);
}

static void onStartClicked(lv_event_t* event) {
    static_cast<UiContext*>(lv_event_get_user_data(event))->motor.requestStartStepTest();
}

static void onStopClicked(lv_event_t* event) {
    static_cast<UiContext*>(lv_event_get_user_data(event))->motor.requestStopStepTest();
}

static void onStepTestChanged(lv_observer_t* observer, lv_subject_t* subject) {
    // START and STOP TEST share a place: one is hidden
    lv_obj_t* stopButton = lv_observer_get_target_obj(observer);
    lv_obj_t* startButton = lv_obj_get_child(lv_obj_get_parent(stopButton), lv_obj_get_index(stopButton) - 1);
    bool isTest = lv_subject_get_int(subject) != 0;

    if (isTest) {
        lv_obj_add_flag(startButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(startButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t* createRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    return row;
}

/**
 * Creates the step test controls: mode and START/STOP TEST; the targets, ramp and dwell are in the list.
 *
 * @param parent The chart's column
 * @param context Motor service, state and navigation
 */
static void createTestControls(lv_obj_t* parent, UiContext& context) {
    UiState::Motor& motor = context.state.motor;

    // Mode and START / STOP TEST
    lv_obj_t* topRow = createRow(parent);

    SegmentedToggleOptions toggleOptions;
    toggleOptions.first = "Velocity";
    toggleOptions.second = "Position";
    toggleOptions.subject = &motor.isStepTestPosition;
    toggleOptions.onChange = onModeChanged;
    toggleOptions.userData = &context;
    toggleOptions.isStretched = true;
    lv_obj_t* toggle = createSegmentedToggle(topRow, toggleOptions);
    lv_obj_set_flex_grow(toggle, 1);

    ButtonOptions startOptions;
    startOptions.text = "START TEST";
    startOptions.icon = Icons::START;
    startOptions.variant = ButtonVariant::PRIMARY;
    startOptions.isCompact = true;
    startOptions.onClick = onStartClicked;
    startOptions.userData = &context;
    lv_obj_t* startButton = createButton(topRow, startOptions);
    lv_obj_set_flex_grow(startButton, 1);

    ButtonOptions stopOptions;
    stopOptions.text = "STOP TEST";
    stopOptions.icon = Icons::STOP;
    stopOptions.variant = ButtonVariant::DANGER;
    stopOptions.isCompact = true;
    stopOptions.onClick = onStopClicked;
    stopOptions.userData = &context;
    lv_obj_t* stopButton = createButton(topRow, stopOptions);
    lv_obj_set_flex_grow(stopButton, 1);
    lv_subject_add_observer_obj(&motor.isStepTest, onStepTestChanged, stopButton, &context);
}

static void onTabDeleted(lv_event_t* event) {
    // Leaving the tab or the screen ends a step test; motion started elsewhere keeps running (the service checks)
    static_cast<UiContext*>(lv_event_get_user_data(event))->motor.requestStopStepTest();
}

lv_obj_t* createTuningTab(lv_obj_t* parent, UiContext& context) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* tab = lv_obj_create(parent);
    lv_obj_remove_style_all(tab);
    lv_obj_set_width(tab, lv_pct(100));
    lv_obj_set_flex_grow(tab, 1);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, Theme::SPACE_L, 0);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tab, onTabDeleted, LV_EVENT_DELETE, &context);

    // Left: the chart, swapped when position mode starts or stops, and the step test under it
    lv_obj_t* left = lv_obj_create(tab);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, lv_pct(CHART_PERCENT), lv_pct(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, Theme::SPACE_S, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* holder = lv_obj_create(left);
    lv_obj_remove_style_all(holder);
    lv_obj_set_width(holder, lv_pct(100));
    lv_obj_set_flex_grow(holder, 1);
    lv_obj_set_flex_flow(holder, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
    lv_subject_add_observer_obj(&motor.isRunning, onMotionChanged, holder, &context);
    lv_subject_add_observer_obj(&motor.motionMode, onMotionChanged, holder, &context);

    createTestControls(left, context);

    // Right: the tuning list
    lv_obj_t* right = lv_obj_create(tab);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    createTuningList(right, context);

    return tab;
}
