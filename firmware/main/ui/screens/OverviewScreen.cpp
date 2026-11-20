#include "OverviewScreen.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "BoardConfig.hpp"
#include "esp_twai_types.h"
#include "protocol/Cia402.hpp"
#include "protocol/ParameterFormat.hpp"
#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Badge.hpp"
#include "ui/components/InfoPanel.hpp"
#include "ui/components/SearchPanel.hpp"
#include "ui/components/ValueTile.hpp"

// Name column widths of the panels
static constexpr int32_t IDENTITY_NAME_WIDTH = 90;
static constexpr int32_t SETTINGS_NAME_WIDTH = 130;
static constexpr int32_t STATE_NAME_WIDTH = 96;

// Panel widths relative to each other; settings has the longest values, state the bit chips
static constexpr uint8_t IDENTITY_PANEL_GROW = 3;
static constexpr uint8_t SETTINGS_PANEL_GROW = 4;
static constexpr uint8_t STATE_PANEL_GROW = 4;

// Tile widths relative to each other; position needs room for a sign, three decimals and "turns"
static constexpr uint8_t TILE_GROW = 2;
static constexpr uint8_t POSITION_TILE_GROW = 3;
static constexpr int32_t TILE_HEIGHT = 104;

// NiMotion vendor ID (1018:01), as the PTU firmware expects it
static constexpr int32_t NIMOTION_VENDOR_ID = 0x509;

// Homing method the PTU firmware requires before it uses a motor 
static constexpr int32_t PTU_HOMING_METHOD = 21;

/**
 * One statusword bit shown as a chip. Static, so it can be observer user data.
 */
struct StatuswordChip {
    uint16_t mask;    // Bit in 6041
    const char* name; // Chip text
};

// CiA 402 statusword bits (CiA 402 §6041h); bit 5 is active low, so its chip names the "not active" meaning
static const StatuswordChip STATUSWORD_BITS[] = {
    {0x0001, "Ready to switch on"},
    {0x0002, "Switched on"},
    {0x0004, "Operation enabled"},
    {0x0008, "Fault"},
    {0x0010, "Voltage enabled"},
    {0x0020, "No quick stop"},
    {0x0040, "Switch on disabled"},
    {0x0080, "Warning"},
    {0x0200, "Remote"},
    {0x0400, "Target reached"},
    {0x0800, "Internal limit"},
};

/**
 * Returns the CiA 402 meaning of a quick stop option code (605A).
 *
 * @param code Value of 605A
 * @return Short description
 */
static const char* getQuickStopOptionName(int32_t code) {
    // CiA 402 §605Ah; 0-4 end in Switch On Disabled, 5-8 stay in Quick Stop Active
    switch (code) {
        case 0:
            return "power off, coast";

        case 1:
            return "normal ramp, then off";

        case 2:
            return "quick-stop ramp, then off";

        case 3:
            return "current limit, then off";

        case 4:
            return "voltage limit, then off";

        case 5:
            return "normal ramp, stay";

        case 6:
            return "quick-stop ramp, stay";

        case 7:
            return "current limit, stay";

        case 8:
            return "voltage limit, stay";
    }

    return "not a CiA 402 option";
}

/**
 * Converts the drive's counts per second (or per second²) to rpm (or rpm/s).
 *
 * @param counts Value in encoder counts
 * @param encoderIncrements Counts per revolution (608F:01), 0 if unknown
 * @return The value in revolutions per minute, 0 if the resolution is unknown
 */
static int64_t countsToRpm(int64_t counts, int32_t encoderIncrements) {
    if (encoderIncrements <= 0) {
        return 0;
    }

    return counts * 60 / encoderIncrements;
}

/**
 * Returns the motor state group an observer was registered with.
 *
 * @param observer The observer; its user data is the UiState
 * @return The motor subjects
 */
static UiState::Motor& getMotor(lv_observer_t* observer) {
    return static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
}

static void onVendorChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t vendorId = lv_subject_get_int(subject);

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "0x%" PRIX32 "%s", static_cast<uint32_t>(vendorId), vendorId == NIMOTION_VENDOR_ID ? " NiMotion" : "");
}

static void onHexChanged(lv_observer_t* observer, lv_subject_t* subject) {
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "0x%" PRIX32, static_cast<uint32_t>(lv_subject_get_int(subject)));
}

static void onSerialChanged(lv_observer_t* observer, lv_subject_t* subject) {
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRIu32, static_cast<uint32_t>(lv_subject_get_int(subject)));
}

static void onBitrateChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t code = lv_subject_get_int(subject);
    const Parameters::Entry* entry = Parameters::find(Objects::BITRATE.index, Objects::BITRATE.subIndex);
    char name[32] = "?";

    // The table's names for the codes (manual §3.6.1; 8 and 6 checked on the bench, 2026-10-12)
    if (entry != nullptr) {
        ParameterFormat::formatValue(*entry, static_cast<uint32_t>(code), 0, name, sizeof(name));
    }

    lv_label_set_text(lv_observer_get_target_obj(observer), name);
}

static void onWatchdogChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);
    int32_t watchdogMs = lv_subject_get_int(&motor.watchdogMs);
    int32_t heartbeatMs = lv_subject_get_int(&motor.heartbeatMs);
    int32_t originalHeartbeatMs = lv_subject_get_int(&motor.originalHeartbeatMs);
    char watchdogText[24];
    char heartbeatText[40];

    // The 200C:1B watchdog doesn't trip on this firmware; the heartbeat is what stops the motor on a lost bus (D-023).
    // The heartbeat the motor came with shows when the console changed it, as run-time writes must (safety rules)
    if (watchdogMs == 0) {
        snprintf(watchdogText, sizeof(watchdogText), "Off");
    } else {
        snprintf(watchdogText, sizeof(watchdogText), "%" PRId32 " ms", watchdogMs);
    }

    if (heartbeatMs != originalHeartbeatMs) {
        snprintf(heartbeatText, sizeof(heartbeatText), "heartbeat %" PRId32 " ms (had %" PRId32 ")", heartbeatMs, originalHeartbeatMs);
    } else {
        snprintf(heartbeatText, sizeof(heartbeatText), "heartbeat %" PRId32 " ms", heartbeatMs);
    }

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s, %s", watchdogText, heartbeatText);
}

static void onUndervoltageLimitChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);
    int32_t limitV = lv_subject_get_int(&motor.undervoltageLimitV);
    int32_t originalLimitV = lv_subject_get_int(&motor.originalUndervoltageLimitV);

    // The value the motor came with is a finding when the console lowered it (D-005)
    if (originalLimitV != limitV) {
        lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 " V (motor had %" PRId32 " V, not saved)", limitV, originalLimitV);
    } else {
        lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 " V", limitV);
    }
}

static void onPtuSetupChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t homingMethod = lv_subject_get_int(subject);

    // The PTU firmware refuses a motor whose homing method isn't 21
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s (homing %" PRId32 ")", homingMethod == PTU_HOMING_METHOD ? "yes" : "no", homingMethod);
}

static void onChangedReactionsChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t count = lv_subject_get_int(subject);

    if (count == 0) {
        lv_label_set_text(lv_observer_get_target_obj(observer), "factory");
    } else {
        lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 " differ from factory", count);
    }
}

static void onQuickStopDecelerationChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);
    int32_t counts = lv_subject_get_int(&motor.quickStopDecelerationCounts);
    int64_t rpmPerSecond = countsToRpm(static_cast<uint32_t>(counts), lv_subject_get_int(&motor.encoderIncrements));

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId64 " rpm/s", rpmPerSecond);
}

static void onQuickStopOptionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t code = lv_subject_get_int(subject);

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 ": %s", code, getQuickStopOptionName(code));
}

static void onMaxVelocityChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);
    int32_t counts = lv_subject_get_int(&motor.maxProfileVelocityCounts);
    int64_t rpm = countsToRpm(static_cast<uint32_t>(counts), lv_subject_get_int(&motor.encoderIncrements));

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId64 " rpm", rpm);
}

static void onCanChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);
    const char* stateName = "unknown";

    switch (lv_subject_get_int(&motor.canErrorState)) {
        case TWAI_ERROR_ACTIVE:
            stateName = "OK";
            break;

        case TWAI_ERROR_WARNING:
            stateName = "Warning";
            break;

        case TWAI_ERROR_PASSIVE:
            stateName = "Error passive";
            break;

        case TWAI_ERROR_BUS_OFF:
            stateName = "Bus-off";
            break;
    }

    // Node and bitrate only when the search found the drive away from node 1 at 1 Mbit/s (D-026)
    char whereText[40] = "";
    int32_t nodeId = lv_subject_get_int(&motor.busNodeId);
    int32_t bitrate = lv_subject_get_int(&motor.busBitrate);

    if (nodeId != BoardConfig::DRIVE_NODE_ID || bitrate != static_cast<int32_t>(BoardConfig::CAN_BITRATE)) {
        snprintf(whereText, sizeof(whereText), ", node %" PRId32 " at %" PRId32 " kbit/s", nodeId, bitrate / 1000);
    }

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer),
                          "%s, TX err %" PRId32 ", RX err %" PRId32 ", drops %" PRId32 "%s",
                          stateName,
                          lv_subject_get_int(&motor.canTransmitErrors),
                          lv_subject_get_int(&motor.canReceiveErrors),
                          lv_subject_get_int(&motor.canReceiveDrops),
                          whereText);
}

static void onRatedSpeedChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getMotor(observer);

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%" PRId32 " / %" PRId32 " rpm", lv_subject_get_int(&motor.ratedSpeedRpm), lv_subject_get_int(&motor.maxMotorSpeedRpm));
}

static void onDriveStateChanged(lv_observer_t* observer, lv_subject_t* subject) {
    DriveState state = static_cast<DriveState>(lv_subject_get_int(subject));

    lv_label_set_text(lv_observer_get_target_obj(observer), getDriveStateName(state));
}

static void onStatuswordBitChanged(lv_observer_t* observer, lv_subject_t* subject) {
    const StatuswordChip* bit = static_cast<const StatuswordChip*>(lv_observer_get_user_data(observer));
    lv_obj_t* chip = lv_observer_get_target_obj(observer);
    bool isSet = (lv_subject_get_int(subject) & bit->mask) != 0;

    // Set bits stand out; the fault and warning bits in their own colours
    uint32_t color = Theme::TEXT;

    if (bit->mask == 0x0008) {
        color = Theme::DANGER;
    } else if (bit->mask == 0x0080) {
        color = Theme::WARNING;
    }

    setBadge(chip, nullptr, bit->name, isSet ? color : Theme::TEXT_MUTED);
    lv_obj_set_style_opa(chip, isSet ? LV_OPA_COVER : LV_OPA_40, 0);
}

void OverviewScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_row(content, Theme::SPACE_M, 0);

    // Without a drive: where the search is and what to check (D-033, moved from the home screen)
    createSearchPanel(content, context.state);

    // With a drive: the tiles and the panels
    lv_obj_t* drive = lv_obj_create(content);
    lv_obj_remove_style_all(drive);
    lv_obj_set_width(drive, lv_pct(100));
    lv_obj_set_flex_grow(drive, 1);
    lv_obj_set_flex_flow(drive, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(drive, Theme::SPACE_M, 0);
    lv_obj_remove_flag(drive, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(drive, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_bind_flag_if_eq(drive, &context.state.motor.isConnected, LV_OBJ_FLAG_HIDDEN, 0);

    createTiles(drive);

    // Three flat sections side by side under the tiles, with dividers between them
    lv_obj_t* panels = lv_obj_create(drive);
    lv_obj_remove_style_all(panels);
    lv_obj_set_width(panels, lv_pct(100));
    lv_obj_set_flex_grow(panels, 1);
    lv_obj_set_flex_flow(panels, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(panels, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_top(panels, Theme::SPACE_S, 0);
    lv_obj_remove_flag(panels, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panels, LV_OBJ_FLAG_CLICKABLE);

    createIdentityPanel(panels);
    createDivider(panels);
    createSettingsPanel(panels);
    createDivider(panels);
    createStatePanel(panels);
}

void OverviewScreen::createTiles(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* tiles = lv_obj_create(parent);
    lv_obj_remove_style_all(tiles);
    lv_obj_set_size(tiles, lv_pct(100), TILE_HEIGHT);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tiles, Theme::SPACE_M, 0);
    lv_obj_remove_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(tiles, LV_OBJ_FLAG_CLICKABLE);

    // Native units, scaled by the tile: current in 0.01 A, bus voltage in 0.1 V
    const struct {
        ValueTileOptions options;
        uint8_t grow;
    } definitions[] = {
        {{"Velocity", "rpm", &motor.velocityRpm, 0, nullptr}, TILE_GROW},
        {{"Position", "turns", &motor.positionMilliturns, 3, nullptr}, POSITION_TILE_GROW},
        {{"Motor current", "A", &motor.currentCentiamps, 2, nullptr}, TILE_GROW},
        {{"Bus voltage", "V", &motor.busVoltageDeciV, 1, &motor.isBelowPackCutoff}, TILE_GROW},
        {{"Temperature", "°C", &motor.temperatureC, 0, nullptr}, TILE_GROW},
    };

    for (const auto& definition : definitions) {
        lv_obj_t* tile = createValueTile(tiles, definition.options);
        lv_obj_set_height(tile, lv_pct(100));
        lv_obj_set_flex_grow(tile, definition.grow);
    }
}

void OverviewScreen::createIdentityPanel(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;
    UiState* state = &context.state;

    lv_obj_t* panel = createInfoPanel(parent, "Drive");
    lv_obj_set_height(panel, lv_pct(100));
    lv_obj_set_flex_grow(panel, IDENTITY_PANEL_GROW);

    lv_label_bind_text(addInfoRow(panel, "Name", IDENTITY_NAME_WIDTH), &motor.deviceName, nullptr);
    Bindings::observeAll(addInfoRow(panel, "Serial", IDENTITY_NAME_WIDTH), onSerialChanged, state, {&motor.serialNumber});
    Bindings::observeAll(addInfoRow(panel, "Vendor", IDENTITY_NAME_WIDTH), onVendorChanged, state, {&motor.vendorId});
    Bindings::observeAll(addInfoRow(panel, "Product", IDENTITY_NAME_WIDTH), onHexChanged, state, {&motor.productCode});
    lv_label_bind_text(addInfoRow(panel, "Hardware", IDENTITY_NAME_WIDTH), &motor.hardwareVersion, nullptr);
    lv_label_bind_text(addInfoRow(panel, "Software", IDENTITY_NAME_WIDTH), &motor.softwareVersion, nullptr);
    lv_label_bind_text(addInfoRow(panel, "Encoder", IDENTITY_NAME_WIDTH), &motor.positionCounts, "%d counts");

    // How the motor is set up: for a PTU or not, and whether its fault reactions are the manual's
    lv_subject_add_observer_obj(&motor.homingMethod, onPtuSetupChanged, addInfoRow(panel, "PTU", IDENTITY_NAME_WIDTH), state);
    lv_subject_add_observer_obj(&motor.changedReactionCount, onChangedReactionsChanged, addInfoRow(panel, "Reactions", IDENTITY_NAME_WIDTH), state);
}

void OverviewScreen::createSettingsPanel(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;
    UiState* state = &context.state;

    lv_obj_t* panel = createInfoPanel(parent, "Settings");
    lv_obj_set_height(panel, lv_pct(100));
    lv_obj_set_flex_grow(panel, SETTINGS_PANEL_GROW);

    lv_label_bind_text(addInfoRow(panel, "Node ID", SETTINGS_NAME_WIDTH), &motor.nodeId, "%d");
    Bindings::observeAll(addInfoRow(panel, "Bitrate", SETTINGS_NAME_WIDTH), onBitrateChanged, state, {&motor.bitrateCode});
    Bindings::observeAll(addInfoRow(panel, "Watchdog", SETTINGS_NAME_WIDTH), onWatchdogChanged, state, {&motor.watchdogMs, &motor.heartbeatMs, &motor.originalHeartbeatMs});
    lv_label_bind_text(addInfoRow(panel, "Encoder", SETTINGS_NAME_WIDTH), &motor.encoderIncrements, "%d counts/rev");
    Bindings::observeAll(addInfoRow(panel, "Rated / max", SETTINGS_NAME_WIDTH), onRatedSpeedChanged, state, {&motor.ratedSpeedRpm, &motor.maxMotorSpeedRpm});
    Bindings::bindFixedPoint(addInfoRow(panel, "Rated current", SETTINGS_NAME_WIDTH), &motor.ratedCurrentCentiamps, 2, "A");
    Bindings::bindFixedPoint(addInfoRow(panel, "Stator R", SETTINGS_NAME_WIDTH), &motor.statorResistanceMilliohms, 3, "Ω");
    Bindings::observeAll(addInfoRow(panel, "Undervoltage", SETTINGS_NAME_WIDTH), onUndervoltageLimitChanged, state, {&motor.undervoltageLimitV, &motor.originalUndervoltageLimitV});
    Bindings::observeAll(addInfoRow(panel, "Quick stop", SETTINGS_NAME_WIDTH), onQuickStopOptionChanged, state, {&motor.quickStopOptionCode});
    Bindings::observeAll(addInfoRow(panel, "Stop decel.", SETTINGS_NAME_WIDTH), onQuickStopDecelerationChanged, state, {&motor.quickStopDecelerationCounts, &motor.encoderIncrements});
    Bindings::observeAll(addInfoRow(panel, "Max velocity", SETTINGS_NAME_WIDTH), onMaxVelocityChanged, state, {&motor.maxProfileVelocityCounts, &motor.encoderIncrements});
}

void OverviewScreen::createStatePanel(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;
    UiState* state = &context.state;

    lv_obj_t* panel = createInfoPanel(parent, "State");
    lv_obj_set_height(panel, lv_pct(100));
    lv_obj_set_flex_grow(panel, STATE_PANEL_GROW);

    Bindings::observeAll(addInfoRow(panel, "CiA 402", STATE_NAME_WIDTH), onDriveStateChanged, state, {&motor.state});
    lv_label_bind_text(addInfoRow(panel, "Statusword", STATE_NAME_WIDTH), &motor.statusword, "0x%04X");
    Bindings::observeAll(addInfoRow(panel, "CAN", STATE_NAME_WIDTH),
                         onCanChanged,
                         state,
                         {&motor.canErrorState, &motor.canTransmitErrors, &motor.canReceiveErrors, &motor.canReceiveDrops, &motor.busNodeId, &motor.busBitrate});

    // One chip per bit, wrapping
    lv_obj_t* chips = lv_obj_create(panel);
    lv_obj_remove_style_all(chips);
    lv_obj_set_size(chips, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(chips, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_row(chips, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_top(chips, Theme::SPACE_S, 0);
    lv_obj_remove_flag(chips, LV_OBJ_FLAG_CLICKABLE);

    for (const StatuswordChip& bit : STATUSWORD_BITS) {
        lv_obj_t* chip = createBadge(chips, true);
        lv_subject_add_observer_obj(&motor.statusword, onStatuswordBitChanged, chip, const_cast<StatuswordChip*>(&bit));
    }
}
