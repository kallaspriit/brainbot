#include "TopBar.hpp"

#include <cinttypes>
#include <cstdio>

#include "esp_twai_types.h"
#include "protocol/AlarmCodes.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/ScreenRegistry.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Badge.hpp"
#include "ui/components/BatteryIndicator.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/StopButton.hpp"
#include "ui/fonts/Icons.hpp"

// CiA 402 statusword bit 7: the drive reports a warning

/**
 * Formats an error code for a badge: its short name, or the prefix and the raw code if it isn't in the alarm table.
 *
 * @param buffer Output buffer
 * @param bufferSize Size of the buffer
 * @param errorCode Error code from 603F
 * @param unknownPrefix Text before the raw code, e.g. "Fault"; may be empty
 */
static void formatAlarm(char* buffer, size_t bufferSize, uint16_t errorCode, const char* unknownPrefix) {
    const AlarmCode* alarm = AlarmCodes::find(errorCode);

    if (alarm != nullptr) {
        snprintf(buffer, bufferSize, "%s", alarm->shortName);
    } else if (unknownPrefix[0] != '\0') {
        snprintf(buffer, bufferSize, "%s 0x%04X", unknownPrefix, errorCode);
    } else {
        snprintf(buffer, bufferSize, "0x%04X", errorCode);
    }
}

/**
 * Returns whether a drive state is a fault.
 *
 * @param state The drive state
 * @return True for Fault and Fault Reaction Active
 */
static bool isFaultState(DriveState state) {
    return state == DriveState::FAULT || state == DriveState::FAULT_REACTION_ACTIVE;
}

/**
 * Creates a plain container for a row of bar items.
 *
 * @param parent Parent object
 * @return The container
 */
static lv_obj_t* createRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Theme::SPACE_M, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    return row;
}

TopBar::TopBar(UiContext& context)
    : context(context) {
    bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), Theme::TOP_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, Theme::color(Theme::SURFACE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, Theme::color(Theme::BORDER), 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_pad_hor(bar, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_column(bar, Theme::SPACE_M, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Back and title share a container that takes the free space, so hiding back on home doesn't move the right side
    lv_obj_t* titleGroup = createRow(bar);
    lv_obj_set_flex_grow(titleGroup, 1);
    lv_obj_set_style_pad_column(titleGroup, Theme::SPACE_S, 0);

    // Back: a square icon button, a full touch target
    ButtonOptions backOptions;
    backOptions.icon = Icons::BACK;
    backOptions.variant = ButtonVariant::SECONDARY;
    backOptions.height = Theme::TOUCH_TARGET_MIN - Theme::SPACE_S;
    backOptions.onClick = onBackClicked;
    backOptions.userData = this;
    backButton = createButton(titleGroup, backOptions);
    lv_obj_set_width(backButton, Theme::TOUCH_TARGET_MIN);
    lv_obj_set_style_pad_hor(backButton, 0, 0);

    titleLabel = lv_label_create(titleGroup);
    lv_obj_set_style_text_font(titleLabel, Theme::FONT_TITLE, 0);
    lv_obj_set_style_text_color(titleLabel, Theme::color(Theme::TEXT), 0);
    lv_obj_set_style_pad_left(titleLabel, Theme::SPACE_S, 0);

    // Home: the robot's name, styled like the splash's
    brandGroup = createRow(titleGroup);
    lv_obj_set_style_pad_column(brandGroup, Theme::SPACE_M, 0);
    lv_obj_set_style_pad_left(brandGroup, Theme::SPACE_S, 0);

    lv_obj_t* brandLabel = lv_label_create(brandGroup);
    lv_label_set_text(brandLabel, "SPARKY");
    lv_obj_set_style_text_font(brandLabel, Theme::FONT_TITLE_SMALL, 0);
    lv_obj_set_style_text_color(brandLabel, Theme::color(Theme::TEXT_MUTED), 0);
    lv_obj_set_style_text_letter_space(brandLabel, BRAND_LETTER_SPACE, 0);

    createDriveGroup();
    createStopArea();
}

void TopBar::setTitle(const char* title) {
    lv_label_set_text(titleLabel, title);
}

void TopBar::setHome(bool isHome) {
    // Home shows the brand; the other screens back and the title
    if (isHome) {
        lv_obj_add_flag(backButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(titleLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(brandGroup, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(backButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(titleLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(brandGroup, LV_OBJ_FLAG_HIDDEN);
    }
}

void TopBar::createDriveGroup() {
    UiState::Motor& motor = context.state.motor;

    driveGroup = createRow(bar);

    // Identity: name over serial
    lv_obj_t* identity = lv_obj_create(driveGroup);
    lv_obj_remove_style_all(identity);
    lv_obj_set_size(identity, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(identity, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(identity, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(identity, LV_OBJ_FLAG_CLICKABLE);

    nameLabel = lv_label_create(identity);
    lv_obj_add_style(nameLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(nameLabel, Theme::FONT_BODY_SMALL, 0);

    serialLabel = lv_label_create(identity);
    lv_obj_add_style(serialLabel, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(serialLabel, Theme::FONT_BODY, 0);

    // Badges; a fault or warning badge opens the Faults screen (Faults plan, D1)
    stateBadge = createBadge(driveGroup);
    errorBadge = createBadge(driveGroup);

    for (lv_obj_t* badge : {stateBadge, errorBadge}) {
        lv_obj_add_style(badge, &Theme::pressedStyle, LV_STATE_PRESSED);
        lv_obj_add_event_cb(badge, onBadgeClicked, LV_EVENT_CLICKED, this);
    }

    lv_obj_add_flag(errorBadge, LV_OBJ_FLAG_CLICKABLE);

    // Pack level from the bus voltage, then the voltage itself in fixed-width digits so it doesn't jump
    createBatteryIndicator(driveGroup, context.state);

    voltageLabel = lv_label_create(driveGroup);
    lv_obj_set_style_text_font(voltageLabel, Theme::FONT_NUMBER_SMALL, 0);
    Bindings::bindFixedPoint(voltageLabel, &motor.busVoltageDeciV, 1, "V");
    lv_subject_add_observer_obj(&motor.busVoltageDeciV, onVoltageReadingChanged, voltageLabel, nullptr);

    // Shown instead of the drive part without a drive
    // Without a drive: what the console is doing; tapping it goes home, where the search and checklist are
    connectionGroup = createRow(bar);
    lv_obj_add_flag(connectionGroup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(connectionGroup, &Theme::pressedStyle, LV_STATE_PRESSED);
    lv_obj_set_style_radius(connectionGroup, Theme::RADIUS, 0);
    lv_obj_set_style_pad_all(connectionGroup, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_column(connectionGroup, Theme::SPACE_S, 0);
    lv_obj_set_ext_click_area(connectionGroup, Theme::SPACE_S);
    lv_obj_add_event_cb(connectionGroup, onConnectionClicked, LV_EVENT_CLICKED, this);

    connectionIcon = lv_label_create(connectionGroup);
    lv_label_set_text(connectionIcon, Icons::INFO);
    lv_obj_set_style_text_font(connectionIcon, Theme::FONT_ICON_SMALL, 0);

    connectionLabel = lv_label_create(connectionGroup);
    lv_obj_set_style_text_font(connectionLabel, Theme::FONT_BODY, 0);

    // The bar lives as long as the app, so it can be the observers' user data
    lv_subject_add_observer_obj(&context.state.system.isCanStarted, onConnectionChanged, bar, this);
    lv_subject_add_observer_obj(&motor.isConnected, onConnectionChanged, bar, this);
    lv_subject_add_observer_obj(&motor.deviceName, onIdentityChanged, bar, this);
    lv_subject_add_observer_obj(&motor.serialNumber, onIdentityChanged, bar, this);
    lv_subject_add_observer_obj(&motor.state, onStateChanged, bar, this);
    lv_subject_add_observer_obj(&motor.statusword, onStateChanged, bar, this);
    lv_subject_add_observer_obj(&motor.isRunning, onStateChanged, bar, this);
    lv_subject_add_observer_obj(&motor.errorCode, onStateChanged, bar, this);
    lv_subject_add_observer_obj(&motor.errorCode, onErrorChanged, bar, this);
    lv_subject_add_observer_obj(&motor.state, onErrorChanged, bar, this);
    lv_subject_add_observer_obj(&motor.statusword, onErrorChanged, bar, this);
    lv_subject_add_observer_obj(&motor.isBelowPackCutoff, onCutoffChanged, bar, this);
}

void TopBar::createStopArea() {
    UiState::Motor& motor = context.state.motor;

    canIcon = lv_label_create(bar);
    lv_label_set_text(canIcon, Icons::CAN_BUS);
    lv_obj_set_style_text_font(canIcon, Theme::FONT_ICON_SMALL, 0);
    lv_subject_add_observer_obj(&motor.canErrorState, onCanChanged, bar, this);
    lv_subject_add_observer_obj(&context.state.system.isCanStarted, onCanChanged, bar, this);

    // STOP at the right edge, usable whenever a drive is connected
    stopButton = createStopButton(bar, context, true);
    lv_obj_set_width(stopButton, STOP_WIDTH);
}

void TopBar::onBackClicked(lv_event_t* event) {
    static_cast<TopBar*>(lv_event_get_user_data(event))->context.navigator.back();
}

void TopBar::onVoltageReadingChanged(lv_observer_t* observer, lv_subject_t* subject) {
    // Hidden until the first reading after a connect, so "0.0 V" never flashes up
    if (lv_subject_get_int(subject) > 0) {
        lv_obj_remove_flag(lv_observer_get_target_obj(observer), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lv_observer_get_target_obj(observer), LV_OBJ_FLAG_HIDDEN);
    }
}

void TopBar::onBadgeClicked(lv_event_t* event) {
    static_cast<TopBar*>(lv_event_get_user_data(event))->context.navigator.open(ScreenId::FAULTS);
}

void TopBar::onConnectionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    static_cast<TopBar*>(lv_observer_get_user_data(observer))->updateConnection();
}

void TopBar::setMotorPartOptional(bool isOptional) {
    isMotorPartOptional = isOptional;
    updateConnection();
}

void TopBar::updateConnection() {
    bool isCanStarted = lv_subject_get_int(&context.state.system.isCanStarted);
    bool isConnected = lv_subject_get_int(&context.state.motor.isConnected);
    bool isPresent = isCanStarted && isConnected;

    // On a screen without a motor the motor part goes while no drive answers; STOP comes back with a drive (D-036)
    bool isMotorPartHidden = isMotorPartOptional && !isPresent;

    for (lv_obj_t* part : {canIcon, stopButton}) {
        // The observers first fire while the bar is built, before the stop area exists
        if (part == nullptr) {
            continue;
        }

        if (isMotorPartHidden) {
            lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (isPresent) {
        lv_obj_remove_flag(driveGroup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(connectionGroup, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    lv_obj_add_flag(driveGroup, LV_OBJ_FLAG_HIDDEN);

    if (isMotorPartHidden) {
        lv_obj_add_flag(connectionGroup, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    lv_obj_remove_flag(connectionGroup, LV_OBJ_FLAG_HIDDEN);

    if (!isCanStarted) {
        lv_label_set_text(connectionLabel, "CAN failed to start");
        lv_obj_set_style_text_color(connectionLabel, Theme::color(Theme::DANGER), 0);
    } else {
        lv_label_set_text(connectionLabel, "Searching for a motor");
        lv_obj_set_style_text_color(connectionLabel, Theme::color(Theme::TEXT_MUTED), 0);
    }

    lv_obj_set_style_text_color(connectionIcon, lv_obj_get_style_text_color(connectionLabel, LV_PART_MAIN), 0);
}

void TopBar::onConnectionClicked(lv_event_t* event) {
    // Home shows the search and what to check
    static_cast<TopBar*>(lv_event_get_user_data(event))->context.navigator.home();
}

void TopBar::onIdentityChanged(lv_observer_t* observer, lv_subject_t* subject) {
    TopBar* topBar = static_cast<TopBar*>(lv_observer_get_user_data(observer));
    UiState::Motor& motor = topBar->context.state.motor;
    const char* name = lv_subject_get_string(&motor.deviceName);

    lv_label_set_text(topBar->nameLabel, name[0] != '\0' ? name : "Unknown drive");
    lv_label_set_text_fmt(topBar->serialLabel, "SN %" PRIu32, static_cast<uint32_t>(lv_subject_get_int(&motor.serialNumber)));
}

void TopBar::onStateChanged(lv_observer_t* observer, lv_subject_t* subject) {
    TopBar* topBar = static_cast<TopBar*>(lv_observer_get_user_data(observer));
    UiState::Motor& motor = topBar->context.state.motor;
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool hasWarning = lv_subject_get_int(&motor.statusword) & StatuswordBit::WARNING;
    const char* text = "Unknown";
    const char* icon = nullptr;
    uint32_t color = Theme::TEXT_MUTED;
    char alarmText[32];

    // Short names; the full state name is on the Overview screen. A fault shows what the fault is instead
    if (isFaultState(state)) {
        uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));

        if (errorCode != 0) {
            formatAlarm(alarmText, sizeof(alarmText), errorCode, "Fault");
            text = alarmText;
        } else {
            text = "Fault";
        }

        setBadge(topBar->stateBadge, Icons::FAULT, text, Theme::DANGER);
        lv_obj_add_flag(topBar->stateBadge, LV_OBJ_FLAG_CLICKABLE);

        return;
    }

    switch (state) {
        case DriveState::NOT_READY_TO_SWITCH_ON:
            text = "Not ready";
            break;

        case DriveState::SWITCH_ON_DISABLED:
            text = "Disabled";
            break;

        case DriveState::READY_TO_SWITCH_ON:
            text = "Ready";
            break;

        case DriveState::SWITCHED_ON:
            text = "Switched on";
            break;

        case DriveState::OPERATION_ENABLED:
            text = lv_subject_get_int(&motor.isRunning) ? "Running" : "Enabled";
            color = Theme::SUCCESS;
            break;

        case DriveState::QUICK_STOP_ACTIVE:
            text = "Quick stop";
            color = Theme::WARNING;
            break;

        case DriveState::FAULT_REACTION_ACTIVE:
        case DriveState::FAULT:
        case DriveState::UNKNOWN:
            break;
    }

    // The warning bit or a halting alarm (stall: Operation enabled, neither bit) turns the state amber
    uint16_t statusword = static_cast<uint16_t>(lv_subject_get_int(&motor.statusword));
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));

    if (hasWarning || isHaltAlarm(statusword, errorCode)) {
        color = Theme::WARNING;
    }

    setBadge(topBar->stateBadge, icon, text, color);
    lv_obj_remove_flag(topBar->stateBadge, LV_OBJ_FLAG_CLICKABLE);
}

void TopBar::onErrorChanged(lv_observer_t* observer, lv_subject_t* subject) {
    TopBar* topBar = static_cast<TopBar*>(lv_observer_get_user_data(observer));
    UiState::Motor& motor = topBar->context.state.motor;
    int32_t errorCode = lv_subject_get_int(&motor.errorCode);
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    uint16_t statusword = static_cast<uint16_t>(lv_subject_get_int(&motor.statusword));

    // In a fault the state badge already names it. Otherwise the code shows with the warning bit or as a halt; after
    // power-on the drive reports 0x7501 for about a second with neither bit set, which stays hidden (bench 2026-10-11)
    if (errorCode == 0 || isFaultState(state) || !isAlarmActive(statusword, static_cast<uint16_t>(errorCode))) {
        lv_obj_add_flag(topBar->errorBadge, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    char text[32];
    formatAlarm(text, sizeof(text), static_cast<uint16_t>(errorCode), "");
    setBadge(topBar->errorBadge, Icons::WARNING, text, Theme::WARNING);
    lv_obj_remove_flag(topBar->errorBadge, LV_OBJ_FLAG_HIDDEN);
}

void TopBar::onCutoffChanged(lv_observer_t* observer, lv_subject_t* subject) {
    TopBar* topBar = static_cast<TopBar*>(lv_observer_get_user_data(observer));
    bool isBelowCutoff = lv_subject_get_int(&topBar->context.state.motor.isBelowPackCutoff);

    lv_obj_set_style_text_color(topBar->voltageLabel, Theme::color(isBelowCutoff ? Theme::WARNING : Theme::TEXT), 0);
}

void TopBar::onCanChanged(lv_observer_t* observer, lv_subject_t* subject) {
    TopBar* topBar = static_cast<TopBar*>(lv_observer_get_user_data(observer));
    UiState& state = topBar->context.state;
    uint32_t color = Theme::SUCCESS;

    if (!lv_subject_get_int(&state.system.isCanStarted)) {
        color = Theme::DANGER;
    } else {
        switch (lv_subject_get_int(&state.motor.canErrorState)) {
            case TWAI_ERROR_ACTIVE:
                color = Theme::SUCCESS;
                break;

            case TWAI_ERROR_WARNING:
            case TWAI_ERROR_PASSIVE:
                color = Theme::WARNING;
                break;

            default:
                color = Theme::DANGER;
                break;
        }
    }

    lv_obj_set_style_text_color(topBar->canIcon, Theme::color(color), 0);
}
