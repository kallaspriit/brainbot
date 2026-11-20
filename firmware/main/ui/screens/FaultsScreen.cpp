#include "FaultsScreen.hpp"

#include <cinttypes>
#include <cstdio>

#include "protocol/AlarmCodes.hpp"
#include "protocol/AlarmWord.hpp"
#include "services/MotorService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/RestoreDefaults.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/ConfirmDialog.hpp"
#include "ui/components/FaultReactionsDialog.hpp"
#include "ui/components/InfoPanel.hpp"
#include "ui/components/MessageLine.hpp"
#include "ui/fonts/Icons.hpp"

// Name column widths of the panels
static constexpr int32_t ACTIVE_NAME_WIDTH = 130;
static constexpr int32_t HISTORY_NUMBER_WIDTH = 32;
static constexpr int32_t LOG_TIME_WIDTH = 76;

// Columns: the left one holds the buttons and a scrolling log, the right one the 16 history rows
static constexpr uint8_t LEFT_COLUMN_GROW = 3;
static constexpr uint8_t RIGHT_COLUMN_GROW = 2;
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
 * Tells whether the drive reports an active alarm (fault, warning or halt; isAlarmActive in Cia402.hpp).
 *
 * @param motor Motor subjects
 * @return True if an alarm is active
 */
static bool isAlarmActive(UiState::Motor& motor) {
    uint16_t statusword = static_cast<uint16_t>(lv_subject_get_int(&motor.statusword));
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));

    return ::isAlarmActive(statusword, errorCode);
}

/**
 * Finds the reaction the drive is configured for, for an alarm code.
 *
 * @param record The fault record
 * @param code Alarm code (603F)
 * @param word Receives the 200E word when found
 * @return True if 200E has an entry for the code
 */
static bool findReaction(const FaultRecord& record, uint16_t code, uint32_t& word) {
    for (size_t entry = 0; entry < record.reactionCount; entry++) {
        if (AlarmWord::getCode(record.reactions[entry]) == code) {
            word = record.reactions[entry];

            return true;
        }
    }

    return false;
}

static void onHeadlineChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));
    bool isFault = (lv_subject_get_int(&motor.statusword) & StatuswordBit::FAULT) != 0;

    // Fault bit: fault. Warning bit: warning, the drive keeps going. Neither: a halt in Operation enabled, else nothing
    // active whatever 603F says
    if (!isAlarmActive(motor)) {
        lv_label_set_text(label, "No active fault");
        lv_obj_set_style_text_color(label, Theme::color(Theme::SUCCESS), 0);

        return;
    }

    const AlarmCode* alarm = AlarmCodes::find(errorCode);

    if (alarm != nullptr) {
        lv_label_set_text(label, alarm->shortName);
    } else if (errorCode != 0) {
        lv_label_set_text_fmt(label, "Alarm 0x%04X", errorCode);
    } else {
        lv_label_set_text(label, "Fault, no error code");
    }

    lv_obj_set_style_text_color(label, Theme::color(isFault ? Theme::DANGER : Theme::WARNING), 0);
}

static void onCodeChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));
    const AlarmCode* alarm = AlarmCodes::find(errorCode);
    const char* name = alarm != nullptr ? alarm->name : "not in the alarm table";
    lv_obj_t* label = lv_observer_get_target_obj(observer);

    if (errorCode == 0) {
        lv_label_set_text(label, "0x0000");
    } else if (isAlarmActive(motor)) {
        lv_label_set_text_fmt(label, "0x%04X, %s", errorCode, name);
    } else {
        lv_label_set_text_fmt(label, "0x%04X, %s; no fault or warning bit (start-up or leftover)", errorCode, name);
    }
}

static void onReactionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&context.state.motor.errorCode));

    if (errorCode == 0 || !isAlarmActive(context.state.motor)) {
        lv_label_set_text(label, "-");

        return;
    }

    // The reaction this drive is configured for; alarms outside 200E have a fixed one (manual §9.2)
    MotorStatus status = context.motor.getStatus();
    uint32_t word = 0;

    // Alarms outside 200E have a built-in reaction (manual table 9-1, mostly fault and needs a reset); the statusword
    // says which it is
    if (!findReaction(status.faultRecord, errorCode, word)) {
        bool isFault = (lv_subject_get_int(&context.state.motor.statusword) & StatuswordBit::FAULT) != 0;
        lv_label_set_text(label, isFault ? "fault, needs a reset (fixed)" : "warning (fixed)");

        return;
    }

    char text[64];
    AlarmWord::formatReaction(word, text, sizeof(text));
    lv_label_set_text_fmt(label, "%s (0x%08" PRIX32 ")", text, word);
}

static void onStatuswordBitsChanged(lv_observer_t* observer, lv_subject_t* subject) {
    int32_t statusword = lv_subject_get_int(subject);
    bool isFault = (statusword & StatuswordBit::FAULT) != 0;
    bool hasWarning = (statusword & StatuswordBit::WARNING) != 0;
    const char* bits = "none";

    if (isFault && hasWarning) {
        bits = "fault, warning";
    } else if (isFault) {
        bits = "fault";
    } else if (hasWarning) {
        bits = "warning";
    }

    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s (0x%04" PRIX32 ")", bits, static_cast<uint32_t>(statusword));
}

static void onErrorRegisterChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    MotorStatus status = context.motor.getStatus();
    char text[64];

    AlarmWord::formatErrorRegister(status.faultRecord.errorRegister, text, sizeof(text));
    lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "0x%02X, %s", status.faultRecord.errorRegister, text);
}

/**
 * Adds one alarm history row: number, name with reaction, and the raw alarm word.
 *
 * @param list History list
 * @param number Position in the history, 1 = newest
 * @param word Alarm word
 */
static void addHistoryRow(lv_obj_t* list, size_t number, uint32_t word) {
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* numberLabel = lv_label_create(row);
    lv_label_set_text_fmt(numberLabel, "%u", static_cast<unsigned>(number));
    lv_obj_set_width(numberLabel, HISTORY_NUMBER_WIDTH);
    lv_obj_add_style(numberLabel, &Theme::mutedTextStyle, 0);

    // Name and, in one word, the reaction recorded with it; an unknown code shows the code
    uint16_t code = AlarmWord::getCode(word);
    const AlarmCode* alarm = AlarmCodes::find(code);
    lv_obj_t* nameLabel = lv_label_create(row);
    lv_obj_set_flex_grow(nameLabel, 1);
    lv_obj_add_style(nameLabel, &Theme::textStyle, 0);

    if (alarm != nullptr) {
        lv_label_set_text_fmt(nameLabel, "%s – %s", alarm->shortName, AlarmWord::getResponseShortText(AlarmWord::getResponseCode(word)));
    } else {
        lv_label_set_text_fmt(nameLabel, "Alarm 0x%04X – %s", code, AlarmWord::getResponseShortText(AlarmWord::getResponseCode(word)));
    }

    lv_obj_set_style_text_color(nameLabel, Theme::color(Theme::alarmColor(AlarmWord::getResponseCode(word))), 0);

    // Fixed-width digits, so the values line up
    lv_obj_t* rawLabel = lv_label_create(row);
    lv_label_set_text_fmt(rawLabel, "0x%08" PRIX32, word);
    lv_obj_add_style(rawLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(rawLabel, Theme::FONT_CODE, 0);
}

static void onHistoryChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* list = lv_observer_get_target_obj(observer);
    MotorStatus status = context.motor.getStatus();
    const FaultRecord& record = status.faultRecord;

    lv_obj_clean(list);

    // Nothing to show yet, or the read failed: say which
    if (!status.isConnected || !record.isRead) {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, "Not read yet");
        lv_obj_add_style(label, &Theme::mutedTextStyle, 0);

        return;
    }

    for (size_t entry = 0; entry < record.historyCount; entry++) {
        if (record.history[entry] != 0) {
            addHistoryRow(list, entry + 1, record.history[entry]);
        }
    }

    if (record.historyCount == 0) {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, "No alarms recorded");
        lv_obj_add_style(label, &Theme::mutedTextStyle, 0);
    }

    if (record.readError[0] != '\0') {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text_fmt(label, "Couldn't read further: %s", record.readError);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, Theme::color(Theme::WARNING), 0);
    }
}

/**
 * Formats console uptime as h:mm:ss.
 *
 * @param timeMs Uptime in ms
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
static void formatTime(uint32_t timeMs, char* buffer, size_t bufferSize) {
    uint32_t seconds = timeMs / 1000;

    snprintf(buffer, bufferSize, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, seconds / 3600, (seconds / 60) % 60, seconds % 60);
}

/**
 * Describes what a session log entry says happened.
 *
 * @param entry The entry
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
static void formatEvent(const SessionLogEntry& entry, char* buffer, size_t bufferSize) {
    const AlarmCode* alarm = AlarmCodes::find(entry.code);
    const char* name = alarm != nullptr ? alarm->shortName : "Alarm";

    switch (entry.kind) {
        case SessionLogEntry::Kind::ALARM:
            snprintf(buffer, bufferSize, "%s 0x%04X", name, entry.code);
            break;

        case SessionLogEntry::Kind::CLEARED:
            snprintf(buffer, bufferSize, "Cleared");
            break;

        case SessionLogEntry::Kind::RESET:
            snprintf(buffer, bufferSize, "%s 0x%04X reset by %s", name, entry.code, entry.resetBy);
            break;

        case SessionLogEntry::Kind::CONNECTED:
            snprintf(buffer, bufferSize, "Motor %" PRIu32 " connected", entry.serialNumber);
            break;

        case SessionLogEntry::Kind::LOST:
            snprintf(buffer, bufferSize, "Drive lost");
            break;

        case SessionLogEntry::Kind::POWERED_OFF:
            snprintf(buffer, bufferSize, "Motor powered off");
            break;

        case SessionLogEntry::Kind::RESTORED:
            snprintf(buffer, bufferSize, "Defaults restored");
            break;

        case SessionLogEntry::Kind::RESTORE_REFUSED:
            snprintf(buffer, bufferSize, "Restore refused by the drive");
            break;

        case SessionLogEntry::Kind::SAVED:
            snprintf(buffer, bufferSize, "Parameters saved");
            break;

        case SessionLogEntry::Kind::SAVE_REFUSED:
            snprintf(buffer, bufferSize, "Save refused by the drive");
            break;

        case SessionLogEntry::Kind::RECOVERED:
            snprintf(buffer, bufferSize, "Blank flash: %u objects rewritten, %u refused", entry.writeCount, entry.refusedCount);
            break;

        case SessionLogEntry::Kind::RECOVER_FAILED:
            snprintf(buffer, bufferSize, "Recovery stopped at the %s", entry.resetBy);
            break;
    }
}

/**
 * Describes the motion recorded with a session log entry, for alarms and resets.
 *
 * @param entry The entry
 * @param buffer Receives the text, empty when there's nothing to say
 * @param bufferSize Size of the buffer
 */
static void formatMotion(const SessionLogEntry& entry, char* buffer, size_t bufferSize) {
    buffer[0] = '\0';

    if (!entry.hasMotion || (entry.kind != SessionLogEntry::Kind::ALARM && entry.kind != SessionLogEntry::Kind::RESET)) {
        return;
    }

    // Stopped needs no numbers; a running motor gets its mode, speed and current
    if (entry.mode == MotionMode::NONE) {
        snprintf(buffer, bufferSize, "stopped");

        return;
    }

    const char* mode = "velocity";

    if (entry.mode == MotionMode::POSITION) {
        mode = "position";
    } else if (entry.mode == MotionMode::TORQUE) {
        mode = "torque";
    }

    int32_t magnitude = entry.currentCentiamps < 0 ? -entry.currentCentiamps : entry.currentCentiamps;
    const char* sign = entry.currentCentiamps < 0 ? "-" : "";
    snprintf(buffer, bufferSize, "%s %" PRId32 " rpm, %s%" PRId32 ".%02" PRId32 " A", mode, entry.velocityRpm, sign, magnitude / 100, magnitude % 100);
}

/**
 * Adds one session log row: time, then the event with its count and motion.
 *
 * @param list Session log list
 * @param entry The entry
 * @param record Fault record, for the reaction that colours an alarm
 */
static void addSessionLogRow(lv_obj_t* list, const SessionLogEntry& entry, const FaultRecord& record) {
    char time[16];
    char event[64];
    char motion[64];
    char countText[16] = "";
    formatTime(entry.timeMs, time, sizeof(time));
    formatEvent(entry, event, sizeof(event));
    formatMotion(entry, motion, sizeof(motion));

    if (entry.count > 1) {
        snprintf(countText, sizeof(countText), " ×%u", static_cast<unsigned>(entry.count));
    }

    lv_obj_t* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* timeLabel = lv_label_create(row);
    lv_label_set_text(timeLabel, time);
    lv_obj_set_width(timeLabel, LOG_TIME_WIDTH);
    lv_obj_add_style(timeLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(timeLabel, Theme::FONT_CODE, 0);

    lv_obj_t* eventLabel = lv_label_create(row);
    lv_obj_set_flex_grow(eventLabel, 1);
    lv_label_set_long_mode(eventLabel, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(eventLabel, &Theme::textStyle, 0);
    lv_label_set_text_fmt(eventLabel, "%s%s%s%s", event, countText, motion[0] != '\0' ? " – " : "", motion);

    // Alarms stand out from the bookkeeping lines, coloured by the reaction this drive has for them
    if (entry.kind == SessionLogEntry::Kind::ALARM) {
        uint32_t word = 0;
        uint8_t responseCode = 0;

        // The configured reaction, else the one the drive recorded with it in the history, else a fixed fault
        if (findReaction(record, entry.code, word)) {
            responseCode = AlarmWord::getResponseCode(word);
        } else {
            for (size_t index = 0; index < record.historyCount; index++) {
                if (AlarmWord::getCode(record.history[index]) == entry.code) {
                    responseCode = AlarmWord::getResponseCode(record.history[index]);
                    break;
                }
            }
        }
        lv_obj_set_style_text_color(eventLabel, Theme::color(Theme::alarmColor(responseCode)), 0);
    }
}

static void onSessionLogChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    lv_obj_t* list = lv_observer_get_target_obj(observer);
    SessionLogEntry entries[MotorService::SESSION_LOG_LENGTH];
    size_t count = context.motor.getSessionLog(entries, MotorService::SESSION_LOG_LENGTH);
    MotorStatus status = context.motor.getStatus();

    lv_obj_clean(list);

    if (count == 0) {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, "Nothing since the console started");
        lv_obj_add_style(label, &Theme::mutedTextStyle, 0);

        return;
    }

    for (size_t index = 0; index < count; index++) {
        addSessionLogRow(list, entries[index], status.faultRecord);
    }
}

/**
 * Creates a plain column with no surface: the screen's structure comes from headings and spacing, not boxes.
 *
 * @param parent Parent object
 * @return The column
 */
static lv_obj_t* createColumn(lv_obj_t* parent) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_remove_style_all(column);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, Theme::SPACE_XS, 0);
    lv_obj_set_style_text_font(column, Theme::FONT_BODY_SMALL, 0);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    return column;
}

/**
 * Adds a section heading, with space above it unless it starts the column.
 *
 * @param parent Column
 * @param text Heading text
 * @param isFirst True for the first heading of a column
 */
static void addHeading(lv_obj_t* parent, const char* text, bool isFirst) {
    lv_obj_t* heading = lv_label_create(parent);
    lv_label_set_text(heading, text);
    lv_obj_add_style(heading, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(heading, Theme::FONT_TITLE_SMALL, 0);
    lv_obj_set_style_pad_top(heading, isFirst ? 0 : Theme::SPACE_L, 0);
    lv_obj_set_style_pad_bottom(heading, Theme::SPACE_XS, 0);
}

/**
 * Names an input function that stops or blocks motion (manual table 7-2), or returns nullptr for one that doesn't.
 *
 * @param function Function number from 2003
 * @return Short description, or nullptr
 */
static const char* getBlockingInputName(uint16_t function) {
    switch (function) {
        case 12:
            return "pause";

        case 14:
            return "forward overtravel switch";

        case 15:
            return "reverse overtravel switch";
    }

    return nullptr;
}

static void onRunChecksChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext& context = getContext(observer);
    UiState::Motor& motor = context.state.motor;
    lv_obj_t* label = lv_observer_get_target_obj(observer);
    MotorStatus status = context.motor.getStatus();
    const RunCheckValues& values = status.runChecks;
    char text[320] = "";
    size_t length = 0;

    // Only what's wrong: a motor with nothing to report shows nothing (Faults plan, D9)
    if (lv_subject_get_int(&motor.isConnected) && values.isRead) {
        int32_t busVoltageDeciV = lv_subject_get_int(&motor.busVoltageDeciV);
        int32_t limitV = lv_subject_get_int(&motor.undervoltageLimitV);

        if (busVoltageDeciV > 0 && busVoltageDeciV < limitV * 10) {
            length += snprintf(text + length,
                               sizeof(text) - length,
                               "Won't enable: bus %" PRId32 ".%" PRId32 " V is below its %" PRId32 " V undervoltage limit\n",
                               busVoltageDeciV / 10,
                               busVoltageDeciV % 10,
                               limitV);
        }

        if (values.controlMode != 0 && length < sizeof(text)) {
            length += snprintf(text + length, sizeof(text) - length, "Control mode 2002:01 was %u, not CiA 402: the console sets it for each start, other masters won't\n", values.controlMode);
        }

        for (size_t input = 0; input < RunCheckValues::INPUT_COUNT && length < sizeof(text); input++) {
            const char* name = getBlockingInputName(values.inputFunctions[input]);

            if (name != nullptr) {
                bool isActive = (values.inputStates & (1 << input)) != 0;
                length += snprintf(text + length,
                                   sizeof(text) - length,
                                   "Input DI%u is a %s (2003:%02X = %u), level %s\n",
                                   static_cast<unsigned>(input + 1),
                                   name,
                                   static_cast<unsigned>(3 + input * 2),
                                   values.inputFunctions[input],
                                   isActive ? "high" : "low");
            }
        }
    }

    // A blank parameter flash, and the state between RECOVER and a successful restore (D-031)
    if (lv_subject_get_int(&motor.isConnected) && length < sizeof(text)) {
        if (lv_subject_get_int(&motor.isParameterFlashBlank)) {
            length += snprintf(
                text + length, sizeof(text) - length, "Parameter flash is blank: every stored setting reads 0xFFFF and the drive refuses to save. RECOVER rewrites them and restores defaults\n");
        }

        if (lv_subject_get_int(&motor.isRecoveredNotRestored) && length < sizeof(text)) {
            length += snprintf(text + length, sizeof(text) - length, "Recovered with the console's values, not restored: SAVE is refused until a restore succeeds\n");
        }
    }

    // Drop the last line break; hidden when there's nothing to say
    if (length > 0 && length < sizeof(text) && text[length - 1] == '\n') {
        text[length - 1] = '\0';
    }

    if (text[0] == '\0') {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

        return;
    }

    lv_label_set_text(label, text);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void onFaultResetEnabledChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    bool isConnected = lv_subject_get_int(&motor.isConnected) != 0;
    uint16_t statusword = static_cast<uint16_t>(lv_subject_get_int(&motor.statusword));
    uint16_t errorCode = static_cast<uint16_t>(lv_subject_get_int(&motor.errorCode));
    bool isFault = (statusword & StatuswordBit::FAULT) != 0;

    // A fault or a halt can be reset; warnings clear themselves
    setButtonEnabled(lv_observer_get_target_obj(observer), isConnected && (isFault || isHaltAlarm(statusword, errorCode)));
}

static void onReactionsClicked(lv_event_t* event) {
    openFaultReactionsDialog(*static_cast<UiContext*>(lv_event_get_user_data(event)));
}

static void onConnectedButtonChanged(lv_observer_t* observer, lv_subject_t* subject) {
    setButtonEnabled(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) != 0);
}

static void onRestoreClicked(lv_event_t* event) {
    openRestoreDefaultsConfirm(*static_cast<UiContext*>(lv_event_get_user_data(event)));
}

static void onRestoreEnabledChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isPowered = state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;
    bool isAllowed = lv_subject_get_int(&motor.isConnected) && !lv_subject_get_int(&motor.isRunning) && !isPowered;
    lv_obj_t* button = lv_observer_get_target_obj(observer);

    // RECOVER takes its place while the flash is blank: a plain restore is refused by the drive then, and the row has
    // room for two actions (D-031)
    if (lv_subject_get_int(&motor.isParameterFlashBlank)) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
    }

    // Never under a running or holding motor
    setButtonEnabled(button, isAllowed);
}

static void onFaultResetClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestFaultReset();
}

void FaultsScreen::create(lv_obj_t* content) {
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, Theme::SPACE_L, 0);
    lv_obj_set_style_pad_column(content, Theme::SPACE_L, 0);

    // Left: what's wrong now, this session and the actions. Right: what the drive remembers. No boxes: headings and a
    // divider carry the structure, and everything fits one screen so a single photo shows it
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left = createColumn(content);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_set_flex_grow(left, LEFT_COLUMN_GROW);

    createDivider(content);

    lv_obj_t* right = createColumn(content);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_grow(right, RIGHT_COLUMN_GROW);

    createActiveSection(left);
    createSessionLogSection(left);
    createActions(left);
    createHistorySection(right);
}

void FaultsScreen::createActiveSection(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    // Heading, name and details; REACTIONS floats in the top right corner, level with the heading, so the detail rows
    // below it keep the full width (only the heading and the short fault name sit beside it)
    lv_obj_t* rows = createColumn(parent);
    lv_obj_set_size(rows, lv_pct(100), LV_SIZE_CONTENT);

    ButtonOptions reactionsOptions;
    reactionsOptions.text = "REACTIONS";
    reactionsOptions.icon = Icons::INFO;
    reactionsOptions.variant = ButtonVariant::SECONDARY_OUTLINE;
    reactionsOptions.isCompact = true;
    reactionsOptions.onClick = onReactionsClicked;
    reactionsOptions.userData = &context;
    lv_obj_t* reactionsButton = createButton(rows, reactionsOptions);
    lv_obj_add_flag(reactionsButton, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(reactionsButton, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_subject_add_observer_obj(&motor.isConnected, onConnectedButtonChanged, reactionsButton, &context);

    addHeading(rows, "Active fault", true);

    // The fault's short name, coloured by severity
    lv_obj_t* headline = lv_label_create(rows);
    lv_obj_set_style_text_font(headline, Theme::FONT_TITLE, 0);
    lv_obj_set_style_pad_bottom(headline, Theme::SPACE_XS, 0);
    Bindings::observeAll(headline, onHeadlineChanged, &context, {&motor.errorCode, &motor.statusword});

    // Code and the manual's name, the configured reaction, and the bits that say fault or warning
    lv_obj_t* codeLabel = addInfoRow(rows, "Error code", ACTIVE_NAME_WIDTH);
    Bindings::observeAll(codeLabel, onCodeChanged, &context, {&motor.errorCode, &motor.statusword});

    lv_obj_t* reactionLabel = addInfoRow(rows, "Reaction", ACTIVE_NAME_WIDTH);
    Bindings::observeAll(reactionLabel, onReactionChanged, &context, {&motor.errorCode, &motor.statusword, &motor.faultRecordVersion});

    lv_obj_t* statuswordLabel = addInfoRow(rows, "Statusword", ACTIVE_NAME_WIDTH);
    lv_subject_add_observer_obj(&motor.statusword, onStatuswordBitsChanged, statuswordLabel, &context);

    lv_obj_t* registerLabel = addInfoRow(rows, "Error register", ACTIVE_NAME_WIDTH);
    lv_subject_add_observer_obj(&motor.faultRecordVersion, onErrorRegisterChanged, registerLabel, &context);

    // Run checks: why a motor with no fault won't run, shown only when one fails (D9)
    lv_obj_t* checksLabel = lv_label_create(rows);
    lv_obj_set_width(checksLabel, lv_pct(100));
    lv_label_set_long_mode(checksLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(checksLabel, Theme::color(Theme::WARNING), 0);
    lv_obj_set_style_pad_top(checksLabel, Theme::SPACE_XS, 0);
    Bindings::observeAll(checksLabel,
                         onRunChecksChanged,
                         &context,
                         {&motor.isConnected, &motor.busVoltageDeciV, &motor.undervoltageLimitV, &motor.faultRecordVersion, &motor.isParameterFlashBlank, &motor.isRecoveredNotRestored});
}

void FaultsScreen::createSessionLogSection(lv_obj_t* parent) {
    addHeading(parent, "This session, newest first", false);

    // Takes the free height and scrolls (it stays clickable, or presses go past it and nothing scrolls); rebuilt
    // whenever the service adds or merges an entry
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, Theme::SPACE_XS, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_add_style(list, &Theme::scrollbarStyle, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(list, Theme::SCROLLBAR_WIDTH + Theme::SPACE_S, 0);
    Bindings::observeAll(list, onSessionLogChanged, &context, {&context.state.motor.sessionLogVersion, &context.state.motor.faultRecordVersion});
}

static void onRecoverClicked(lv_event_t* event) {
    openRecoverConfirm(*static_cast<UiContext*>(lv_event_get_user_data(event)));
}

/**
 * Shows RECOVER only for a blank parameter flash, and enables it under the restore's rule (D-031).
 *
 * @param observer Observer with the UI context as user data; its target is the button
 * @param subject The changed subject
 */
static void onRecoverEnabledChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = getContext(observer).state.motor;
    lv_obj_t* button = lv_observer_get_target_obj(observer);
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isPowered = state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;
    bool isBlank = lv_subject_get_int(&motor.isConnected) && lv_subject_get_int(&motor.isParameterFlashBlank);

    if (isBlank) {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }

    setButtonEnabled(button, isBlank && !lv_subject_get_int(&motor.isRunning) && !isPowered);
}

void FaultsScreen::createActions(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    // The outcome of the last action (a reset says whether it cleared, and why not)
    createMessageLine(parent, context.state);

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_top(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // RESET FAULT: a command that can't start motion, so no confirmation (Faults plan scope)
    ButtonOptions resetOptions;
    resetOptions.text = "RESET FAULT";
    resetOptions.icon = Icons::REFRESH;
    resetOptions.variant = ButtonVariant::SECONDARY;
    resetOptions.onClick = onFaultResetClicked;
    resetOptions.userData = &context.motor;
    lv_obj_t* resetButton = createButton(row, resetOptions);
    lv_obj_set_width(resetButton, LV_SIZE_CONTENT);
    Bindings::observeAll(resetButton, onFaultResetEnabledChanged, &context, {&motor.isConnected, &motor.statusword, &motor.errorCode});

    // RECOVER: only for a blank parameter flash, behind a confirm dialog (D-031)
    ButtonOptions recoverOptions;
    recoverOptions.text = "RECOVER";
    recoverOptions.icon = Icons::TOOLS;
    recoverOptions.variant = ButtonVariant::DANGER_OUTLINE;
    recoverOptions.onClick = onRecoverClicked;
    recoverOptions.userData = &context;
    lv_obj_t* recoverButton = createButton(row, recoverOptions);
    lv_obj_set_flex_grow(recoverButton, 1);
    Bindings::observeAll(recoverButton, onRecoverEnabledChanged, &context, {&motor.isConnected, &motor.isRunning, &motor.state, &motor.isParameterFlashBlank});

    // RESTORE DEFAULTS: destructive, so red outline and a confirm dialog (D-025)
    ButtonOptions restoreOptions;
    restoreOptions.text = "FACTORY RESET";
    restoreOptions.icon = Icons::RESET;
    restoreOptions.variant = ButtonVariant::DANGER_OUTLINE;
    restoreOptions.onClick = onRestoreClicked;
    restoreOptions.userData = &context;
    lv_obj_t* restoreButton = createButton(row, restoreOptions);
    lv_obj_set_flex_grow(restoreButton, 1);
    Bindings::observeAll(restoreButton, onRestoreEnabledChanged, &context, {&motor.isConnected, &motor.isRunning, &motor.state, &motor.isParameterFlashBlank});
}

void FaultsScreen::createHistorySection(lv_obj_t* parent) {
    UiState::Motor& motor = context.state.motor;

    addHeading(parent, "Alarm history, newest first", true);

    // Rebuilt whenever the service reads the history again (after an EMCY, a reset, a reconnect)
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, Theme::SPACE_XS, 0);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    Bindings::observeAll(list, onHistoryChanged, &context, {&motor.faultRecordVersion, &motor.isConnected});
}
