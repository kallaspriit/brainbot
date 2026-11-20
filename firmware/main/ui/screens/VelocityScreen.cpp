#include "VelocityScreen.hpp"

#include "services/MotorService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/MessageLine.hpp"
#include "ui/components/NumberControl.hpp"
#include "ui/components/ValueTile.hpp"
#include "ui/components/VelocityChart.hpp"
#include "ui/fonts/Icons.hpp"

// Step buttons; static so they outlive the controls
static const int32_t VELOCITY_STEPS_RPM[] = {-100, -10, -1, 1, 10, 100};
static const int32_t ACCELERATION_STEPS_RPM_PER_SECOND[] = {-100, -10, 10, 100};

// Readout and controls share the width; the controls need room for six step buttons
static constexpr uint8_t READOUT_GROW = 4;
static constexpr uint8_t CONTROLS_GROW = 5;
static constexpr int32_t VELOCITY_TILE_HEIGHT = 120;
static constexpr int32_t SMALL_TILE_HEIGHT = 104;

/**
 * Creates a plain flex container.
 *
 * @param parent Parent object
 * @param flow Flex direction
 * @return The container
 */
static lv_obj_t* createBox(lv_obj_t* parent, lv_flex_flow_t flow) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_flex_flow(box, flow);
    lv_obj_set_style_pad_row(box, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_column(box, Theme::SPACE_M, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    return box;
}

/**
 * Shows START while the drive is off and the ramp STOP while it runs, in the same place.
 *
 * @param observer The observer; its target is the container of both buttons, its user data the UiState
 * @param subject The subject that changed
 */
static void onRunningChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
    lv_obj_t* actions = lv_observer_get_target_obj(observer);
    lv_obj_t* startButton = lv_obj_get_child(actions, 0);
    lv_obj_t* stopButton = lv_obj_get_child(actions, 1);

    // Judged from the drive's state too, so a drive enabled without our run flag still offers a stop
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isRunning = lv_subject_get_int(&motor.isRunning) || state == DriveState::OPERATION_ENABLED;

    if (isRunning) {
        lv_obj_add_flag(startButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(startButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    }
}

void VelocityScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_column(content, Theme::SPACE_L, 0);

    createReadout(content);
    createControls(content);
}

void VelocityScreen::onEnter() {
    context.motor.requestMotionScreen(MotionMode::VELOCITY);
}

void VelocityScreen::createReadout(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* readout = createBox(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(readout, lv_pct(100));
    lv_obj_set_flex_grow(readout, READOUT_GROW);

    // Actual velocity, the number this screen is about
    ValueTileOptions velocityOptions;
    velocityOptions.label = "Velocity";
    velocityOptions.unit = "rpm";
    velocityOptions.subject = &motor.velocityRpm;
    velocityOptions.isLarge = true;
    lv_obj_t* velocityTile = createValueTile(readout, velocityOptions);
    lv_obj_set_size(velocityTile, lv_pct(100), VELOCITY_TILE_HEIGHT);

    // Position in turns from the console zero (D-021), and current, side by side
    lv_obj_t* row = createBox(readout, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row, lv_pct(100), SMALL_TILE_HEIGHT);

    ValueTileOptions positionOptions;
    positionOptions.label = "Position";
    positionOptions.unit = "turns";
    positionOptions.subject = &motor.positionMilliturns;
    positionOptions.decimals = 3;
    lv_obj_t* positionTile = createValueTile(row, positionOptions);
    lv_obj_set_height(positionTile, lv_pct(100));
    lv_obj_set_flex_grow(positionTile, 3);

    ValueTileOptions currentOptions;
    currentOptions.label = "Motor current";
    currentOptions.unit = "A";
    currentOptions.subject = &motor.currentCentiamps;
    currentOptions.decimals = 2;
    lv_obj_t* currentTile = createValueTile(row, currentOptions);
    lv_obj_set_height(currentTile, lv_pct(100));
    lv_obj_set_flex_grow(currentTile, 2);

    // Velocity over the last seconds, to see ramps and overshoot (D-017); the drive state is in the top bar
    lv_obj_t* chart = createVelocityChart(readout, context);
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);

    // Last message, where start failures show their step, state and abort code
    createMessageLine(readout, context.state);
}

void VelocityScreen::createControls(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* controls = createBox(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(controls, lv_pct(100));
    lv_obj_set_flex_grow(controls, CONTROLS_GROW);
    lv_obj_set_style_pad_row(controls, Theme::SPACE_L, 0);

    // Targets live in the motor service; the controls start from them and send changes back
    NumberControlOptions velocityOptions;
    velocityOptions.label = "Target speed";
    velocityOptions.unit = "rpm";
    velocityOptions.subject = &motor.targetVelocityRpm;
    velocityOptions.min = -MotorService::FALLBACK_VELOCITY_LIMIT_RPM;
    velocityOptions.max = MotorService::FALLBACK_VELOCITY_LIMIT_RPM;
    velocityOptions.maxSubject = &motor.velocityLimitRpm; // The motor's rated speed (D-019)
    velocityOptions.isSymmetric = true;
    velocityOptions.overlayLayer = context.navigator.getOverlayLayer();
    velocityOptions.steps = VELOCITY_STEPS_RPM;
    velocityOptions.stepCount = sizeof(VELOCITY_STEPS_RPM) / sizeof(VELOCITY_STEPS_RPM[0]);
    velocityOptions.onChange = onVelocityChanged;
    velocityOptions.userData = &context.motor;
    lv_obj_set_width(createNumberControl(controls, velocityOptions), lv_pct(100));

    NumberControlOptions accelerationOptions;
    accelerationOptions.label = "Acceleration";
    accelerationOptions.unit = "rpm/s";
    accelerationOptions.subject = &motor.targetAccelerationRpmPerSecond;
    accelerationOptions.min = MotorService::MIN_ACCELERATION_RPM_PER_SECOND;
    accelerationOptions.max = MotorService::FALLBACK_ACCELERATION_LIMIT_RPM_PER_SECOND;
    accelerationOptions.maxSubject = &motor.accelerationLimitRpmPerSecond; // The drive's quick-stop deceleration (D-019)
    accelerationOptions.overlayLayer = context.navigator.getOverlayLayer();
    accelerationOptions.steps = ACCELERATION_STEPS_RPM_PER_SECOND;
    accelerationOptions.stepCount = sizeof(ACCELERATION_STEPS_RPM_PER_SECOND) / sizeof(ACCELERATION_STEPS_RPM_PER_SECOND[0]);
    accelerationOptions.onChange = onAccelerationChanged;
    accelerationOptions.userData = &context.motor;
    lv_obj_set_width(createNumberControl(controls, accelerationOptions), lv_pct(100));

    // START, replaced by a ramp STOP while running, at the bottom. The top bar STOP is the quick stop
    lv_obj_t* spacer = createBox(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* actions = createBox(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);

    ButtonOptions startOptions;
    startOptions.text = "START";
    startOptions.icon = Icons::START;
    startOptions.variant = ButtonVariant::PRIMARY;
    startOptions.height = Theme::BUTTON_HEIGHT_LARGE;
    startOptions.onClick = onStartClicked;
    startOptions.userData = &context.motor;
    lv_obj_t* startButton = createButton(actions, startOptions);
    lv_obj_set_width(startButton, lv_pct(100));
    lv_obj_bind_state_if_eq(startButton, &motor.isConnected, LV_STATE_DISABLED, 0);

    ButtonOptions stopOptions;
    stopOptions.text = "SMOOTH STOP";
    stopOptions.icon = Icons::STOP;
    stopOptions.variant = ButtonVariant::DANGER_OUTLINE;
    stopOptions.height = Theme::BUTTON_HEIGHT_LARGE;
    stopOptions.onClick = onRampStopClicked;
    stopOptions.userData = &context.motor;
    lv_obj_t* stopButton = createButton(actions, stopOptions);
    lv_obj_set_width(stopButton, lv_pct(100));

    Bindings::observeAll(actions, onRunningChanged, &context.state, {&motor.isRunning, &motor.state});
}

void VelocityScreen::onVelocityChanged(int32_t velocityRpm, void* userData) {
    static_cast<MotorService*>(userData)->setTargetVelocityRpm(velocityRpm);
}

void VelocityScreen::onAccelerationChanged(int32_t accelerationRpmPerSecond, void* userData) {
    static_cast<MotorService*>(userData)->setAccelerationRpmPerSecond(accelerationRpmPerSecond);
}

void VelocityScreen::onStartClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestStartVelocity();
}

void VelocityScreen::onRampStopClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestRampStop();
}
