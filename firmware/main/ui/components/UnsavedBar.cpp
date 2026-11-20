#include "UnsavedBar.hpp"

#include <cinttypes>
#include <cstdio>

#include "protocol/ParameterFormat.hpp"
#include "services/MotorService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/RestoreDefaults.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/ConfirmDialog.hpp"
#include "ui/fonts/Icons.hpp"

static constexpr size_t MAX_CHANGE_LINES = 8; // Changes listed one by one; the rest as a count
static constexpr size_t LINE_LENGTH = 240;
static constexpr size_t MAX_POINTS = MAX_CHANGE_LINES + 5;

static void onSaveConfirmed(void* userData) {
    static_cast<MotorService*>(userData)->requestSave();
}

/**
 * Builds the confirm's points: what ends up in flash. The user's changes, and the battery values the bus voltage
 * forces (D-028), as change lines too; the console's own values going back to the motor's own changes nothing, so it
 * is said only when factory values go back instead.
 *
 * @param context Motor service and state
 * @param lines Receives the texts
 * @param points Receives pointers to them
 * @return Number of points
 */
static size_t buildPoints(UiContext& context, char (*lines)[LINE_LENGTH], const char** points) {
    MotorStatus status = context.motor.getStatus();
    SavePreview preview = context.motor.getSavePreview();
    UnsavedChange changes[MotorService::MAX_UNSAVED_CHANGES];
    size_t changeCount = context.motor.getUnsavedChanges(changes, MotorService::MAX_UNSAVED_CHANGES);
    uint32_t flashMinimumDeciV = preview.flashMinimumDeciV;
    bool hasRatedVoltageChange = false;
    size_t count = 0;

    // The changes: what, from, to
    for (size_t index = 0; index < changeCount && index < MAX_CHANGE_LINES; index++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[changes[index].position];
        const char* name = entry.description[0] != '\0' ? entry.description : entry.name;
        bool isRatedVoltage = entry.index == Objects::RATED_VOLTAGE.index && entry.subIndex == Objects::RATED_VOLTAGE.subIndex;
        char before[48];
        char now[48];
        ParameterFormat::formatValue(entry, changes[index].before, status.unitsPerTurn, before, sizeof(before));
        ParameterFormat::formatValue(entry, changes[index].now, status.unitsPerTurn, now, sizeof(now));

        // A rated voltage the bus can't keep: say what is saved instead
        if (isRatedVoltage && preview.lowersRatedVoltage) {
            hasRatedVoltageChange = true;

            if (changes[index].before == preview.loweredRatedVoltageV) {
                snprintf(lines[count],
                         LINE_LENGTH,
                         "%s: stays %" PRIu32 " V (%s needs the bus above %" PRIu32 ".%" PRIu32 " V)",
                         name,
                         preview.loweredRatedVoltageV,
                         now,
                         flashMinimumDeciV / 10,
                         flashMinimumDeciV % 10);
            } else {
                snprintf(lines[count],
                         LINE_LENGTH,
                         "%s: %s → %" PRIu32 " V (%s needs the bus above %" PRIu32 ".%" PRIu32 " V)",
                         name,
                         before,
                         preview.loweredRatedVoltageV,
                         now,
                         flashMinimumDeciV / 10,
                         flashMinimumDeciV % 10);
            }
        } else {
            snprintf(lines[count], LINE_LENGTH, "%s: %s → %s", name, before, now);
        }

        count++;
    }

    if (changeCount > MAX_CHANGE_LINES) {
        snprintf(lines[count], LINE_LENGTH, "and %u more", static_cast<unsigned>(changeCount - MAX_CHANGE_LINES));
        count++;
    }

    // Battery values: saved although nobody changed them, because of the bus voltage
    if (preview.keepsBatteryUndervoltage) {
        snprintf(lines[count],
                 LINE_LENGTH,
                 "Undervoltage limit: %" PRIu32 " V → %" PRIu32 " V (battery: the bus is below %" PRIu32 " V)",
                 preview.ownUndervoltageV,
                 preview.batteryUndervoltageV,
                 preview.ownUndervoltageV);
        count++;
    }

    if (preview.lowersRatedVoltage && !hasRatedVoltageChange) {
        snprintf(lines[count],
                 LINE_LENGTH,
                 "Rated voltage: %" PRIu32 " V → %" PRIu32 " V (battery: flash writes need the bus above %" PRIu32 ".%" PRIu32 " V)",
                 preview.ratedVoltageV,
                 preview.loweredRatedVoltageV,
                 flashMinimumDeciV / 10,
                 flashMinimumDeciV % 10);
        count++;
    }

    // Only when the motor's own values are unknown does putting the console's back change anything
    if (preview.usesFactoryForOwn) {
        snprintf(lines[count], LINE_LENGTH, "Heartbeat, position limits, motion mode and ramps: back to factory values (the motor's own are unknown until it is power-cycled)");
        count++;
    }

    for (size_t index = 0; index < count; index++) {
        points[index] = lines[index];
    }

    return count;
}

static void onSaveClicked(lv_event_t* event) {
    UiContext& context = *static_cast<UiContext*>(lv_event_get_user_data(event));
    char lines[MAX_POINTS][LINE_LENGTH];
    const char* points[MAX_POINTS];
    size_t pointCount = buildPoints(context, lines, points);
    char title[48];
    int32_t changeCount = lv_subject_get_int(&context.state.motor.unsavedChangeCount);
    snprintf(title, sizeof(title), "Save %" PRId32 " change%s to the motor?", changeCount, changeCount == 1 ? "" : "s");

    // The dialog copies the texts
    ConfirmDialogOptions options;
    options.title = title;
    options.subtitle = "Saved values stay after a power cycle.";
    options.points = points;
    options.pointCount = pointCount;
    options.writes = "1010:01 = 0x65766173 (save)";
    options.confirmText = "SAVE";
    options.onConfirm = onSaveConfirmed;
    options.userData = &context.motor;
    openConfirmDialog(context.navigator.getOverlayLayer(), options);
}

static void onFactoryResetClicked(lv_event_t* event) {
    openRestoreDefaultsConfirm(*static_cast<UiContext*>(lv_event_get_user_data(event)));
}

static void onRevertClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestRevert();
}

/**
 * Keeps the bar in place (no layout shift) and says whether anything is unsaved; REVERT and SAVE work only when
 * something is, SAVE not while the motor runs, and FACTORY RESET only with the motor stopped and the stage off (as on
 * the Faults screen).
 */
static void onUnsavedChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiContext*>(lv_observer_get_user_data(observer))->state.motor;
    lv_obj_t* bar = lv_observer_get_target_obj(observer);
    lv_obj_t* label = lv_obj_get_child(bar, 0);
    lv_obj_t* factoryResetButton = lv_obj_get_child(bar, 1);
    lv_obj_t* revertButton = lv_obj_get_child(bar, 2);
    lv_obj_t* saveButton = lv_obj_get_child(bar, 3);
    int32_t count = lv_subject_get_int(&motor.unsavedChangeCount);
    bool isRunning = lv_subject_get_int(&motor.isRunning) != 0;
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isPowered = state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;
    bool isConnected = lv_subject_get_int(&motor.isConnected) != 0;

    if (count == 0) {
        lv_label_set_text(label, "No unsaved changes");
        lv_obj_set_style_text_color(label, Theme::color(Theme::TEXT_MUTED), 0);
    } else {
        lv_label_set_text_fmt(label, "%" PRId32 " unsaved change%s, live until power-off", count, count == 1 ? "" : "s");
        lv_obj_set_style_text_color(label, Theme::color(Theme::ACCENT), 0);
    }

    setButtonEnabled(factoryResetButton, isConnected && !isRunning && !isPowered);
    setButtonEnabled(revertButton, count > 0);
    setButtonEnabled(saveButton, count > 0 && !isRunning);
}

lv_obj_t* createUnsavedBar(lv_obj_t* parent, UiContext& context) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, Theme::SPACE_M, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* label = lv_label_create(bar);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_font(label, Theme::FONT_BODY, 0);
    lv_obj_set_style_text_color(label, Theme::color(Theme::ACCENT), 0);

    // FACTORY RESET: the restore-defaults confirm, as on the Setup tab and the Faults screen; kept apart from SAVE by REVERT
    ButtonOptions factoryResetOptions;
    factoryResetOptions.text = "FACTORY RESET";
    factoryResetOptions.variant = ButtonVariant::DANGER_OUTLINE;
    factoryResetOptions.isCompact = true;
    factoryResetOptions.onClick = onFactoryResetClicked;
    factoryResetOptions.userData = &context;
    createButton(bar, factoryResetOptions);

    // REVERT: the earlier values back, live; works while running for the immediate ones
    ButtonOptions revertOptions;
    revertOptions.text = "REVERT";
    revertOptions.variant = ButtonVariant::SECONDARY_OUTLINE;
    revertOptions.isCompact = true;
    revertOptions.onClick = onRevertClicked;
    revertOptions.userData = &context.motor;
    createButton(bar, revertOptions);

    // SAVE: the first write to flash from this screen, so always behind the confirm
    ButtonOptions saveOptions;
    saveOptions.text = "SAVE";
    saveOptions.icon = Icons::CHECK;
    saveOptions.variant = ButtonVariant::PRIMARY;
    saveOptions.isCompact = true;
    saveOptions.onClick = onSaveClicked;
    saveOptions.userData = &context;
    createButton(bar, saveOptions);

    Bindings::observeAll(bar, onUnsavedChanged, &context, {&motor.unsavedChangeCount, &motor.isRunning, &motor.state, &motor.isConnected});

    return bar;
}

void setUnsavedBarFactoryResetShown(lv_obj_t* bar, bool isShown) {
    lv_obj_t* factoryResetButton = lv_obj_get_child(bar, 1);

    if (isShown) {
        lv_obj_remove_flag(factoryResetButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(factoryResetButton, LV_OBJ_FLAG_HIDDEN);
    }
}
