#include "PositionScreen.hpp"

#include "services/MotorService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/MessageLine.hpp"
#include "ui/components/NumberControl.hpp"
#include "ui/components/PositionChart.hpp"
#include "ui/components/SegmentedToggle.hpp"
#include "ui/components/ValueTile.hpp"
#include "ui/fonts/Icons.hpp"

// Steps; static so they outlive the controls
static const int32_t TARGET_STEPS_MILLITURNS[] = {-10000, -1000, -250, 250, 1000, 10000};
static const int32_t SPEED_STEPS_RPM[] = {-100, -10, 10, 100};
static const int32_t ACCELERATION_STEPS_RPM_PER_SECOND[] = {-100, -10, 10, 100};

// Readout and controls share the width
static constexpr uint8_t READOUT_GROW = 4;
static constexpr uint8_t CONTROLS_GROW = 5;
static constexpr int32_t POSITION_TILE_HEIGHT = 120;
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
 * Shows the absolute target or the relative move, in the same place.
 *
 * @param observer The observer; its target is the target area (absolute control, relative control, ends-at line), its
 * user data the UiState
 * @param subject The subject that changed
 */
static void onRelativeChanged(lv_observer_t* observer, lv_subject_t* subject) {
    lv_obj_t* area = lv_observer_get_target_obj(observer);
    bool isRelative = lv_subject_get_int(subject) != 0;

    for (uint32_t index = 0; index < 2; index++) {
        bool isShown = index == 0 ? !isRelative : isRelative;

        if (isShown) {
            lv_obj_remove_flag(lv_obj_get_child(area, index), LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lv_obj_get_child(area, index), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * Shows where a relative move will end: the relative base plus the distance.
 *
 * @param observer The observer; its target is the ends-at label, its user data the UiState
 * @param subject The subject that changed
 */
static void onEndsAtChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;

    // Empty but still a line tall in absolute mode, so switching modes doesn't move the controls below
    if (!lv_subject_get_int(&motor.isPositionRelative)) {
        lv_label_set_text(lv_observer_get_target_obj(observer), "");

        return;
    }

    char endText[24];
    Bindings::formatFixedPoint(endText, sizeof(endText), lv_subject_get_int(&motor.relativeBaseMilliturns) + lv_subject_get_int(&motor.moveByMilliturns), 3, "turns");

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "Ends at %s", endText);
}

/**
 * Shows HALT while moving and RELEASE while holding (at the target or halted), and RELEASE disabled while released (D5).
 *
 * @param observer The observer; its target is the container of both buttons, its user data the UiState
 * @param subject The subject that changed
 */
static void onPositionStateChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
    lv_obj_t* slot = lv_observer_get_target_obj(observer);
    lv_obj_t* haltButton = lv_obj_get_child(slot, 0);
    lv_obj_t* releaseButton = lv_obj_get_child(slot, 1);

    bool isPositionMode = lv_subject_get_int(&motor.isRunning) && lv_subject_get_int(&motor.motionMode) == static_cast<int32_t>(MotionMode::POSITION);
    bool isTargetReached = lv_subject_get_int(&motor.statusword) & StatuswordBit::TARGET_REACHED;
    bool isHolding = isPositionMode && (lv_subject_get_int(&motor.isHalted) || isTargetReached);
    bool isMoving = isPositionMode && !isHolding;

    // RELEASE stays visible but disabled while released, so the buttons never move
    setButtonEnabled(releaseButton, isPositionMode);

    if (isMoving) {
        lv_obj_remove_flag(haltButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(releaseButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(haltButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(releaseButton, LV_OBJ_FLAG_HIDDEN);
    }
}

void PositionScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_column(content, Theme::SPACE_L, 0);

    createReadout(content);
    createControls(content);
}

void PositionScreen::onEnter() {
    context.motor.requestMotionScreen(MotionMode::POSITION);
}

void PositionScreen::createReadout(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* readout = createBox(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(readout, lv_pct(100));
    lv_obj_set_flex_grow(readout, READOUT_GROW);

    // Position from the console zero, the number this screen is about
    ValueTileOptions positionOptions;
    positionOptions.label = "Position";
    positionOptions.unit = "turns";
    positionOptions.subject = &motor.positionMilliturns;
    positionOptions.decimals = 3;
    positionOptions.isLarge = true;
    lv_obj_t* positionTile = createValueTile(readout, positionOptions);
    lv_obj_set_size(positionTile, lv_pct(100), POSITION_TILE_HEIGHT);

    // ZERO in the tile's corner: the console zero to here, nothing written to the drive (D-021)
    ButtonOptions zeroOptions;
    zeroOptions.text = "ZERO";
    zeroOptions.variant = ButtonVariant::SECONDARY_OUTLINE;
    zeroOptions.isCompact = true;
    zeroOptions.onClick = onZeroClicked;
    zeroOptions.userData = &context.motor;
    lv_obj_t* zeroButton = createButton(positionTile, zeroOptions);
    lv_obj_add_flag(zeroButton, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(zeroButton, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_bind_state_if_eq(zeroButton, &motor.isConnected, LV_STATE_DISABLED, 0);

    // Degrees from the zero, and the raw encoder counts for checking against the drive
    lv_obj_t* row = createBox(readout, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(row, lv_pct(100), SMALL_TILE_HEIGHT);

    ValueTileOptions degreesOptions;
    degreesOptions.label = "Angle";
    degreesOptions.unit = "°";
    degreesOptions.subject = &motor.positionDecidegrees;
    degreesOptions.decimals = 1;
    lv_obj_t* degreesTile = createValueTile(row, degreesOptions);
    lv_obj_set_height(degreesTile, lv_pct(100));
    lv_obj_set_flex_grow(degreesTile, 1);

    ValueTileOptions countsOptions;
    countsOptions.label = "Encoder counts";
    countsOptions.unit = "";
    countsOptions.subject = &motor.positionCounts;
    lv_obj_t* countsTile = createValueTile(row, countsOptions);
    lv_obj_set_height(countsTile, lv_pct(100));
    lv_obj_set_flex_grow(countsTile, 1);

    // Actual against target over the last seconds: the ramp, overshoot, and a held shaft pushed off and returning
    lv_obj_t* chart = createPositionChart(readout, context);
    lv_obj_set_width(chart, lv_pct(100));
    lv_obj_set_flex_grow(chart, 1);

    // Last message: moves, halts and failures with their cause
    createMessageLine(readout, context.state);
}

void PositionScreen::createControls(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* controls = createBox(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(controls, lv_pct(100));
    lv_obj_set_flex_grow(controls, CONTROLS_GROW);
    lv_obj_set_style_pad_row(controls, Theme::SPACE_S, 0);

    // Target area: the absolute target or the relative move in the same place, swapped by the toggle in their headers.
    // Both live in the motor service and move nothing until GO (D3, D4)
    lv_obj_t* area = createBox(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(area, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(area, Theme::SPACE_XS, 0);

    NumberControlOptions targetOptions;
    targetOptions.label = "";
    targetOptions.unit = "turns";
    targetOptions.subject = &motor.targetPositionMilliturns;
    targetOptions.min = -MotorService::MAX_POSITION_TARGET_TURNS * 1000;
    targetOptions.max = MotorService::MAX_POSITION_TARGET_TURNS * 1000;
    targetOptions.decimals = 3;
    targetOptions.hasSlider = false;
    targetOptions.steps = TARGET_STEPS_MILLITURNS;
    targetOptions.stepCount = sizeof(TARGET_STEPS_MILLITURNS) / sizeof(TARGET_STEPS_MILLITURNS[0]);
    targetOptions.onChange = onTargetChanged;
    targetOptions.userData = &context.motor;
    targetOptions.overlayLayer = context.navigator.getOverlayLayer();
    lv_obj_t* targetControl = createNumberControl(area, targetOptions);
    lv_obj_set_width(targetControl, lv_pct(100));
    addModeToggle(getNumberControlHeader(targetControl));

    NumberControlOptions moveByOptions = targetOptions;
    moveByOptions.subject = &motor.moveByMilliturns;
    moveByOptions.onChange = onMoveByChanged;
    lv_obj_t* moveByControl = createNumberControl(area, moveByOptions);
    lv_obj_set_width(moveByControl, lv_pct(100));
    addModeToggle(getNumberControlHeader(moveByControl));

    lv_obj_t* endsAtLabel = lv_label_create(area);
    lv_obj_add_style(endsAtLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(endsAtLabel, Theme::FONT_BODY_SMALL, 0);
    Bindings::observeAll(endsAtLabel, onEndsAtChanged, &context.state, {&motor.relativeBaseMilliturns, &motor.moveByMilliturns, &motor.isPositionRelative});

    Bindings::observeAll(area, onRelativeChanged, &context.state, {&motor.isPositionRelative});

    // Move speed (6081) and acceleration (shared with the Velocity screen), used from the next GO; no sliders, to fit
    NumberControlOptions speedOptions;
    speedOptions.label = "Move speed";
    speedOptions.unit = "rpm";
    speedOptions.subject = &motor.moveSpeedRpm;
    speedOptions.min = 1;
    speedOptions.max = MotorService::FALLBACK_VELOCITY_LIMIT_RPM;
    speedOptions.maxSubject = &motor.velocityLimitRpm; // The motor's rated speed (D-019)
    speedOptions.hasSlider = false;
    speedOptions.steps = SPEED_STEPS_RPM;
    speedOptions.stepCount = sizeof(SPEED_STEPS_RPM) / sizeof(SPEED_STEPS_RPM[0]);
    speedOptions.onChange = onMoveSpeedChanged;
    speedOptions.userData = &context.motor;
    speedOptions.overlayLayer = context.navigator.getOverlayLayer();
    lv_obj_set_width(createNumberControl(controls, speedOptions), lv_pct(100));

    NumberControlOptions accelerationOptions;
    accelerationOptions.label = "Acceleration";
    accelerationOptions.unit = "rpm/s";
    accelerationOptions.subject = &motor.targetAccelerationRpmPerSecond;
    accelerationOptions.min = MotorService::MIN_ACCELERATION_RPM_PER_SECOND;
    accelerationOptions.max = MotorService::FALLBACK_ACCELERATION_LIMIT_RPM_PER_SECOND;
    accelerationOptions.maxSubject = &motor.accelerationLimitRpmPerSecond; // The drive's quick-stop deceleration (D-019)
    accelerationOptions.hasSlider = false;
    accelerationOptions.steps = ACCELERATION_STEPS_RPM_PER_SECOND;
    accelerationOptions.stepCount = sizeof(ACCELERATION_STEPS_RPM_PER_SECOND) / sizeof(ACCELERATION_STEPS_RPM_PER_SECOND[0]);
    accelerationOptions.onChange = onAccelerationChanged;
    accelerationOptions.userData = &context.motor;
    accelerationOptions.overlayLayer = context.navigator.getOverlayLayer();
    lv_obj_set_width(createNumberControl(controls, accelerationOptions), lv_pct(100));

    // GO and HALT at the bottom; STOP is in the top bar on every screen
    lv_obj_t* spacer = createBox(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* actions = createBox(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);

    // HALT and RELEASE share the left half; only one shows at a time. No gap inside the slot: LVGL keeps a column gap
    // for a hidden child, which would make the two different widths
    lv_obj_t* slot = createBox(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(slot, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(slot, 1);
    lv_obj_set_style_pad_column(slot, 0, 0);

    ButtonOptions haltOptions;
    haltOptions.text = "HALT";
    haltOptions.icon = Icons::STOP;
    haltOptions.variant = ButtonVariant::DANGER_OUTLINE;
    haltOptions.height = Theme::BUTTON_HEIGHT_LARGE;
    haltOptions.onClick = onHaltClicked;
    haltOptions.userData = &context.motor;
    lv_obj_set_flex_grow(createButton(slot, haltOptions), 1);

    ButtonOptions releaseOptions;
    releaseOptions.text = "RELEASE";
    releaseOptions.icon = Icons::POWER;
    releaseOptions.variant = ButtonVariant::DANGER_OUTLINE;
    releaseOptions.height = Theme::BUTTON_HEIGHT_LARGE;
    releaseOptions.onClick = onReleaseClicked;
    releaseOptions.userData = &context.motor;
    lv_obj_set_flex_grow(createButton(slot, releaseOptions), 1);

    Bindings::observeAll(slot, onPositionStateChanged, &context.state, {&motor.isRunning, &motor.motionMode, &motor.isHalted, &motor.statusword});

    ButtonOptions goOptions;
    goOptions.text = "GO";
    goOptions.icon = Icons::START;
    goOptions.variant = ButtonVariant::PRIMARY;
    goOptions.height = Theme::BUTTON_HEIGHT_LARGE;
    goOptions.onClick = onGoClicked;
    goOptions.userData = &context.motor;
    lv_obj_t* goButton = createButton(actions, goOptions);
    lv_obj_set_flex_grow(goButton, 1);
    lv_obj_bind_state_if_eq(goButton, &motor.isConnected, LV_STATE_DISABLED, 0);
    lv_subject_add_observer_obj(&motor.isGoWithoutEffect, onGoLookChanged, goButton, nullptr);
}

void PositionScreen::addModeToggle(lv_obj_t* header) {
    SegmentedToggleOptions toggleOptions;
    toggleOptions.first = "Absolute";
    toggleOptions.second = "Relative";
    toggleOptions.subject = &context.state.motor.isPositionRelative;
    toggleOptions.onChange = onModeChanged;
    toggleOptions.userData = &context.motor;
    lv_obj_t* toggle = createSegmentedToggle(header, toggleOptions);
    lv_obj_move_to_index(toggle, 0);
}

void PositionScreen::onTargetChanged(int32_t targetMilliturns, void* userData) {
    static_cast<MotorService*>(userData)->setPositionTargetMilliturns(targetMilliturns);
}

void PositionScreen::onMoveByChanged(int32_t distanceMilliturns, void* userData) {
    static_cast<MotorService*>(userData)->setMoveByMilliturns(distanceMilliturns);
}

void PositionScreen::onModeChanged(int32_t index, void* userData) {
    static_cast<MotorService*>(userData)->setPositionRelative(index == 1);
}

void PositionScreen::onMoveSpeedChanged(int32_t speedRpm, void* userData) {
    static_cast<MotorService*>(userData)->setMoveSpeedRpm(speedRpm);
}

void PositionScreen::onAccelerationChanged(int32_t accelerationRpmPerSecond, void* userData) {
    static_cast<MotorService*>(userData)->setAccelerationRpmPerSecond(accelerationRpmPerSecond);
}

void PositionScreen::onGoClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestGoPosition();
}

void PositionScreen::onGoLookChanged(lv_observer_t* observer, lv_subject_t* subject) {
    // Outline while GO wouldn't move the shaft; still tappable (re-sends the target, or locks the shaft)
    bool isWithoutEffect = lv_subject_get_int(subject) != 0;
    setButtonVariant(lv_observer_get_target_obj(observer), isWithoutEffect ? ButtonVariant::PRIMARY_OUTLINE : ButtonVariant::PRIMARY);
}

void PositionScreen::onHaltClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestHalt();
}

void PositionScreen::onReleaseClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestRelease();
}

void PositionScreen::onZeroClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestZeroHere();
}
