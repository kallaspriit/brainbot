#include "CalibrateTab.hpp"

#include <cinttypes>
#include <cstdio>

#include "protocol/Cia402.hpp"
#include "protocol/ParameterFormat.hpp"
#include "protocol/ParameterTable.hpp"
#include "services/MotorService.hpp"
#include "services/MotorStatus.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/ConfirmDialog.hpp"
#include "ui/components/InfoPanel.hpp"
#include "ui/components/MessageLine.hpp"
#include "ui/fonts/Icons.hpp"

static constexpr int32_t NAME_WIDTH = 190; // Name column of both panels

// The result rows, in CalibrationStatus::Constant order
static const char* const CONSTANT_NAMES[CalibrationStatus::CONSTANT_COUNT] = {"Initial angle", "Stator resistance", "Inductance Lq", "Inductance Ld", "Torque coefficient", "Rotor inertia"};
static const ObjectEntry* const CONSTANT_OBJECTS[CalibrationStatus::CONSTANT_COUNT] = {
    &Objects::INITIAL_ANGLE,
    &Objects::STATOR_RESISTANCE,
    &Objects::STATOR_INDUCTANCE_LQ,
    &Objects::STATOR_INDUCTANCE_LD,
    &Objects::TORQUE_COEFFICIENT,
    &Objects::ROTOR_INERTIA,
};

// What the confirmation says (D-032)
static const char* const CALIBRATE_POINTS[] = {
    "The shaft must be free with nothing attached: the drive locks it at up to the open-loop current, then spins it both ways up to 80 % of the rated speed.",
    "Takes 10 to 20 s. STOP in the top bar ends it and puts the stored values back.",
    "The identified values go into the drive's memory, not its flash: SAVE keeps them, REVERT puts the stored values back; then power-cycle the motor before running it.",
    "A residual of a few degrees means the stored angle was right; about 80 degrees on a motor that runs fine means the drive does not use this value, so don't keep it.",
};

/**
 * Returns the UI context an observer was registered with.
 *
 * @param observer The observer; its user data is the UiContext
 * @return The context
 */
static UiContext& getContext(lv_observer_t* observer) {
    return *static_cast<UiContext*>(lv_observer_get_user_data(observer));
}

/**
 * Formats a constant's raw value with the parameter table's unit and decimals, or raw when the table doesn't know it.
 *
 * @param object The object
 * @param raw The value as read
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
static void formatConstant(const ObjectEntry& object, uint32_t raw, char* buffer, size_t bufferSize) {
    const Parameters::Entry* entry = Parameters::find(object.index, object.subIndex);

    if (entry != nullptr) {
        ParameterFormat::formatValue(*entry, raw, 1, buffer, bufferSize);
    } else {
        snprintf(buffer, bufferSize, "%" PRIu32, raw);
    }
}

/**
 * Returns the angle's change in electrical degrees, 65536 per turn, wrapped to -180..180 (inferred unit, bench
 * 2026-10-15).
 *
 * @param before The angle as the drive had it
 * @param after The angle as identified
 * @return Degrees
 */
static int32_t getAngleChangeDegrees(uint32_t before, uint32_t after) {
    int16_t difference = static_cast<int16_t>(static_cast<uint16_t>(after) - static_cast<uint16_t>(before));

    return static_cast<int32_t>(difference) * 360 / 65536;
}

// The parameter model (read when the screen opens); the LVGL task only, so one static buffer is enough
static ParameterValue parameterValues[Parameters::ENTRY_COUNT];

/**
 * Reads an object's current value from the parameter model.
 *
 * @param context UI context
 * @param object The object
 * @param raw Receives the value
 * @return True if the model has read it
 */
static bool getModelValue(UiContext& context, const ObjectEntry& object, uint32_t& raw) {
    const Parameters::Entry* entry = Parameters::find(object.index, object.subIndex);

    if (entry == nullptr) {
        return false;
    }

    size_t position = static_cast<size_t>(entry - Parameters::ENTRIES);
    size_t count = context.motor.getParameterValues(parameterValues, Parameters::ENTRY_COUNT);

    if (position >= count || !parameterValues[position].isRead) {
        return false;
    }

    raw = parameterValues[position].raw;

    return true;
}

// Result rows

static void onResultRowChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    size_t constant = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(label));
    CalibrationStatus calibration = context.motor.getStatus().calibration;
    const ObjectEntry& object = *CONSTANT_OBJECTS[constant];
    char text[96] = "";

    // Before a run: the stored value, from the parameter model
    if (calibration.state == CalibrationStatus::State::IDLE) {
        uint32_t raw = 0;

        if (getModelValue(context, object, raw)) {
            formatConstant(object, raw, text, sizeof(text));
        } else {
            snprintf(text, sizeof(text), "-");
        }

        lv_label_set_text(label, text);

        return;
    }

    char before[32] = "";
    formatConstant(object, calibration.before[constant], before, sizeof(before));

    if (calibration.state != CalibrationStatus::State::DONE) {
        snprintf(text, sizeof(text), "%s", before);
    } else {
        char after[32] = "";
        formatConstant(object, calibration.after[constant], after, sizeof(after));

        if (constant == CalibrationStatus::INITIAL_ANGLE) {
            snprintf(text, sizeof(text), "%s → %s  (%+" PRId32 "° el.)", before, after, getAngleChangeDegrees(calibration.before[constant], calibration.after[constant]));
        } else {
            snprintf(text, sizeof(text), "%s → %s", before, after);
        }
    }

    lv_label_set_text(label, text);
}

static void onResultTitleChanged(lv_observer_t* observer, lv_subject_t* subject) {
    CalibrationStatus::State state = getContext(observer).motor.getStatus().calibration.state;
    lv_label_set_text(lv_observer_get_target_obj(observer), state == CalibrationStatus::State::DONE ? "Stored → identified" : "Stored values");
}

// Status and the start button

static void onStatusChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    CalibrationStatus calibration = context.motor.getStatus().calibration;
    char text[128] = "";

    switch (calibration.state) {
        case CalibrationStatus::State::IDLE:
            snprintf(text, sizeof(text), "Not run this session");
            break;

        case CalibrationStatus::State::RUNNING:
            if (calibration.stage <= 1) {
                snprintf(text, sizeof(text), "Locking the shaft, %" PRIu32 " s", calibration.elapsedMs / 1000);
            } else if (calibration.stage == 2) {
                snprintf(text, sizeof(text), "Spinning, %" PRId32 " rpm, %" PRIu32 " s", calibration.speedRpm, calibration.elapsedMs / 1000);
            } else {
                snprintf(text, sizeof(text), "Finishing, %" PRId32 " rpm", calibration.speedRpm);
            }

            break;

        case CalibrationStatus::State::DONE:
            snprintf(text, sizeof(text), "Done in %" PRIu32 " s: SAVE keeps it, REVERT discards", calibration.elapsedMs / 1000);
            break;

        case CalibrationStatus::State::FAILED:
            snprintf(text, sizeof(text), "Failed: %s", calibration.reason);
            break;

        case CalibrationStatus::State::CANCELLED:
            snprintf(text, sizeof(text), "Stopped");
            break;
    }

    lv_label_set_text(label, text);
}

static void onCalibrateConfirmed(void* userData) {
    static_cast<MotorService*>(userData)->requestCalibration();
}

static void onCalibrateClicked(lv_event_t* event) {
    UiContext& context = *static_cast<UiContext*>(lv_event_get_user_data(event));

    ConfirmDialogOptions options;
    options.title = "Calibrate the motor?";
    options.subtitle = "The drive measures its own initial angle, resistance, inductance, Kt and inertia.";
    options.points = CALIBRATE_POINTS;
    options.pointCount = sizeof(CALIBRATE_POINTS) / sizeof(CALIBRATE_POINTS[0]);
    options.writes = "2017:01 = 1, 2000:1E = 0, 2002:01 = 5, 2031:01 = 1; all put back afterwards";
    options.confirmText = "CALIBRATE";
    options.onConfirm = onCalibrateConfirmed;
    options.userData = &context.motor;
    openConfirmDialog(context.navigator.getOverlayLayer(), options);
}

/**
 * CALIBRATE is allowed under FACTORY RESET's rule, and not while the flash is blank, a power cycle is due or a run is
 * on (D-032).
 */
static void onCalibrateEnabledChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isPowered = state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;
    bool isRunning = lv_subject_get_int(&motor.calibrationState) == static_cast<int32_t>(CalibrationStatus::State::RUNNING);
    bool isAllowed = lv_subject_get_int(&motor.isConnected) && !lv_subject_get_int(&motor.isRunning) && !isPowered && !lv_subject_get_int(&motor.isParameterFlashBlank) &&
                     !lv_subject_get_int(&motor.needsPowerCycle) && !isRunning;

    setButtonEnabled(lv_observer_get_target_obj(observer), isAllowed);
}

static void onRatedSpeedChanged(lv_observer_t* observer, lv_subject_t* subject) {
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 " rpm, spins up to %" PRId32, lv_subject_get_int(subject), lv_subject_get_int(subject) * 8 / 10);
}

static void onOpenLoopCurrentChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    uint32_t raw = 0;

    if (!getModelValue(context, Objects::OPEN_LOOP_CURRENT, raw)) {
        lv_label_set_text(label, "reading");

        return;
    }

    char text[32] = "";
    formatConstant(Objects::OPEN_LOOP_CURRENT, raw, text, sizeof(text));
    lv_label_set_text(label, text);
}

lv_obj_t* createCalibrateTab(lv_obj_t* parent, UiContext& context) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* tab = lv_obj_create(parent);
    lv_obj_remove_style_all(tab);
    lv_obj_set_width(tab, lv_pct(100));
    lv_obj_set_flex_grow(tab, 1);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, Theme::SPACE_L, 0);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    // Left: what the run does with this motor, the status, CALIBRATE
    lv_obj_t* left = lv_obj_create(tab);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, Theme::SPACE_S, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = createInfoPanel(left, "Offline identification");
    lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);

    lv_obj_t* currentLabel = addInfoRow(panel, "Open-loop current", NAME_WIDTH);
    Bindings::observeAll(currentLabel, onOpenLoopCurrentChanged, &context, {&motor.parametersVersion, &motor.isConnected});

    lv_obj_t* speedLabel = addInfoRow(panel, "Rated speed", NAME_WIDTH);
    lv_subject_add_observer_obj(&motor.ratedSpeedRpm, onRatedSpeedChanged, speedLabel, &context);

    lv_obj_t* statusLabel = addInfoRow(panel, "Status", NAME_WIDTH);
    lv_subject_add_observer_obj(&motor.calibrationVersion, onStatusChanged, statusLabel, &context);

    // The outcome of the last action; what the run does is in the confirm dialog
    lv_obj_t* spacer = lv_obj_create(left);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    createMessageLine(left, context.state);

    ButtonOptions calibrateOptions;
    calibrateOptions.text = "CALIBRATE";
    calibrateOptions.icon = Icons::TOOLS;
    calibrateOptions.variant = ButtonVariant::PRIMARY;
    calibrateOptions.onClick = onCalibrateClicked;
    calibrateOptions.userData = &context;
    lv_obj_t* calibrateButton = createButton(left, calibrateOptions);
    lv_obj_set_width(calibrateButton, lv_pct(100));
    Bindings::observeAll(
        calibrateButton, onCalibrateEnabledChanged, &context, {&motor.isConnected, &motor.isRunning, &motor.state, &motor.isParameterFlashBlank, &motor.needsPowerCycle, &motor.calibrationState});

    // Right: the result, stored -> identified
    lv_obj_t* right = lv_obj_create(tab);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* result = createInfoPanel(right, "Stored values");
    lv_obj_set_size(result, lv_pct(100), LV_SIZE_CONTENT);
    lv_subject_add_observer_obj(&motor.calibrationVersion, onResultTitleChanged, lv_obj_get_child(result, 0), &context);

    for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
        lv_obj_t* label = addInfoRow(result, CONSTANT_NAMES[constant], NAME_WIDTH);
        lv_obj_set_user_data(label, reinterpret_cast<void*>(static_cast<uintptr_t>(constant)));
        Bindings::observeAll(label, onResultRowChanged, &context, {&motor.calibrationVersion, &motor.parametersVersion});
    }

    return tab;
}
