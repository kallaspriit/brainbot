#include "SetupList.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"
#include "protocol/AlarmWord.hpp"
#include "protocol/ParameterFormat.hpp"
#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/ParameterEditor.hpp"
#include "ui/RestoreDefaults.hpp"
#include "ui/StepTestValues.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/ConfirmDialog.hpp"
#include "ui/components/NumberPad.hpp"
#include "ui/components/OptionPicker.hpp"
#include "ui/fonts/Icons.hpp"

namespace {
/**
 * How a setting is changed.
 */
enum class Control : uint8_t {
    NUMBER,   // Number pad in the entry's unit
    GAIN,     // A number with - / + steppers of about 10 % that write live (Tuning, PAR-009)
    CHOICE,   // A named choice (options)
    REACTION, // A 200E fault reaction: reaction code and self-reset
    SHOW,     // Shown, not changed here (a console session value)
    BUS,      // Node ID or bitrate: a red confirm, applies after a power cycle (PAR-008)
    RESTORE,  // The restore-defaults button; no table entry
    TEST,     // A step test setting (PAR-010), not a drive parameter; subIndex holds the StepTestValue
};

/**
 * One setting on the Setup tab, pointing at its table entry.
 */
struct SetupItem {
    uint16_t index;
    uint8_t subIndex;
    const char* label; // Short name; nullptr for the entry's description
    Control control;
    const PickerOption* options; // For CHOICE
    size_t optionCount;
};

struct SetupSection {
    const char* title;
    const char* subtitle; // What the group is for; empty for none
    const SetupItem* items;
    size_t itemCount;
};

/**
 * What the list keeps between rebuilds; freed with the list.
 */
struct SetupListState {
    UiContext* context = nullptr;
    lv_obj_t* list = nullptr;
    const SetupSection* sections = nullptr; // The Setup or the Tuning sections
    size_t sectionCount = 0;
    int32_t valueWidth = 0; // Value button width: narrower in the Tuning tab's half
    ParameterValue values[Parameters::ENTRY_COUNT];
    UnsavedChange unsaved[MotorService::MAX_UNSAVED_CHANGES];
    size_t unsavedCount = 0;
    int32_t unitsPerTurn = 0;
    bool hasEarlierSessionValues = false;
    bool isCreating = true; // Observers fire once as they are added; the list is built once after them instead
};

/**
 * The setting a picker changes. One picker at a time, and it must outlive the picker, so it is file-wide.
 */
struct PickTarget {
    UiContext* context = nullptr;
    size_t position = 0;
    uint32_t word = 0;        // REACTION: the fault reaction word being changed
    bool isSelfReset = false; // REACTION: the picker sets the self-reset byte, not the reaction code
};

PickTarget pickTarget;

/**
 * The node ID or bitrate waiting for its confirmation; file-wide for the same reason.
 */
struct BusTarget {
    UiContext* context = nullptr;
    size_t position = 0;
    uint32_t raw = 0;
};

BusTarget busTarget;
} // namespace

// Option lists, from the Chinese manual (CN line numbers)

// Digital input functions, table 7-2 (CN:3722-3752); the pulse and encoder inputs (40-48) are left out
static const PickerOption DI_FUNCTIONS[] = {
    {0, "No function"},
    {1, "Drive enable (NiMotion modes)"},
    {2, "Alarm reset"},
    {12, "Pause"},
    {14, "Forward limit switch"},
    {15, "Reverse limit switch"},
    {20, "Step increment enable"},
    {28, "Multi-step enable"},
    {31, "Home switch"},
    {33, "Set origin"},
    {36, "Analog input direction"},
    {38, "Clear fault history"},
    {39, "Clear power-on time"},
};

// Physical input logic, table 7-3 (CN:3760-3768)
static const PickerOption DI_LOGIC[] = {
    {0, "Active low"},
    {1, "Active high"},
    {2, "Rising edge"},
    {3, "Falling edge"},
    {4, "Both edges"},
};

// Homing methods 17-30 (CN:2063, 2107-2345); 21 is what the PTU firmware requires
static const PickerOption HOMING_METHODS[] = {
    {17, "17: reverse limit switch"},
    {18, "18: forward limit switch"},
    {19, "19: home switch, forward"},
    {20, "20: home switch, forward edge"},
    {21, "21: home switch, reverse (PTU)"},
    {22, "22: home switch, reverse edge"},
    {23, "23: home switch + fwd limit"},
    {24, "24: home switch + fwd limit"},
    {25, "25: home switch + fwd limit"},
    {26, "26: home switch + fwd limit"},
    {27, "27: home switch + rev limit"},
    {28, "28: home switch + rev limit"},
    {29, "29: home switch + rev limit"},
    {30, "30: home switch + rev limit"},
};

// Fault reaction codes, §9.2 (CN:4998-5033); any configurable alarm takes any of them
static const PickerOption REACTIONS[] = {
    {0, "Fault: power off, coast"},
    {1, "Fault: slow ramp, power off"},
    {2, "Fault: quick ramp, power off"},
    {3, "Halt and hold, no fault"},
    {4, "Warning, keep running"},
    {5, "Ignore, record in history"},
    {6, "Ignore"},
};

// Bitrate codes of 200C:03 (manual §3.6.1), fastest first
static const PickerOption BITRATES[] = {
    {8, "1 Mbit/s"},
    {7, "800 kbit/s"},
    {6, "500 kbit/s"},
    {5, "250 kbit/s"},
    {4, "125 kbit/s"},
    {3, "100 kbit/s"},
    {2, "50 kbit/s"},
    {1, "20 kbit/s"},
    {0, "10 kbit/s"},
};

static const PickerOption SELF_RESET[] = {
    {0, "Needs a reset"},
    {1, "Clears itself"},
};

// The settings

static const SetupItem LIMIT_ITEMS[] = {
    {0x2000, 0x09, "Rated current", Control::NUMBER, nullptr, 0},
    {0x2000, 0x0A, "Maximum current", Control::NUMBER, nullptr, 0},
    {0x2000, 0x0B, "Maximum current time", Control::NUMBER, nullptr, 0},
    {0x6073, 0x00, "Current limit", Control::NUMBER, nullptr, 0},
};

static const SetupItem RAMP_ITEMS[] = {
    {0x607F, 0x00, "Maximum profile speed", Control::NUMBER, nullptr, 0},
    {0x6085, 0x00, "Quick-stop deceleration", Control::NUMBER, nullptr, 0},
    {0x60C5, 0x00, "Maximum acceleration", Control::NUMBER, nullptr, 0},
    {0x60C6, 0x00, "Maximum deceleration", Control::NUMBER, nullptr, 0},
};

static const SetupItem REACTION_ITEMS[] = {
    {0x200E, 0x01, "Overload", Control::REACTION, nullptr, 0},
    {0x200E, 0x02, "Stall", Control::REACTION, nullptr, 0},
    {0x200E, 0x03, "Overvoltage", Control::REACTION, nullptr, 0},
    {0x200E, 0x04, "Undervoltage", Control::REACTION, nullptr, 0},
    {0x200E, 0x05, "Overtemperature", Control::REACTION, nullptr, 0},
    {0x200E, 0x06, "Undertemperature", Control::REACTION, nullptr, 0},
    {0x200E, 0x07, "Bad parameter", Control::REACTION, nullptr, 0},
    {0x200E, 0x08, "Overspeed", Control::REACTION, nullptr, 0},
    {0x200E, 0x09, "Communication failure", Control::REACTION, nullptr, 0},
    {0x200E, 0x0A, "Homing timeout", Control::REACTION, nullptr, 0},
    {0x200E, 0x0B, "Following error", Control::REACTION, nullptr, 0},
    {0x200E, 0x0C, "Software position limit", Control::REACTION, nullptr, 0},
    {0x200E, 0x0D, "Limit switch", Control::REACTION, nullptr, 0},
    {0x200E, 0x0E, "Phase loss", Control::REACTION, nullptr, 0},
    {0x200E, 0x0F, "Motion profile error", Control::REACTION, nullptr, 0},
    {0x200E, 0x10, "Target position overflow", Control::REACTION, nullptr, 0},
    {0x200E, 0x11, "Profile parameters too small", Control::REACTION, nullptr, 0},
};

static const SetupItem INPUT_ITEMS[] = {
    {0x2003, 0x03, "DI1 function", Control::CHOICE, DI_FUNCTIONS, sizeof(DI_FUNCTIONS) / sizeof(DI_FUNCTIONS[0])},
    {0x2003, 0x04, "DI1 logic", Control::CHOICE, DI_LOGIC, sizeof(DI_LOGIC) / sizeof(DI_LOGIC[0])},
    {0x2003, 0x05, "DI2 function", Control::CHOICE, DI_FUNCTIONS, sizeof(DI_FUNCTIONS) / sizeof(DI_FUNCTIONS[0])},
    {0x2003, 0x06, "DI2 logic", Control::CHOICE, DI_LOGIC, sizeof(DI_LOGIC) / sizeof(DI_LOGIC[0])},
    {0x2003, 0x07, "DI3 function", Control::CHOICE, DI_FUNCTIONS, sizeof(DI_FUNCTIONS) / sizeof(DI_FUNCTIONS[0])},
    {0x2003, 0x08, "DI3 logic", Control::CHOICE, DI_LOGIC, sizeof(DI_LOGIC) / sizeof(DI_LOGIC[0])},
};

static const SetupItem HOMING_ITEMS[] = {
    {0x6098, 0x00, "Homing method", Control::CHOICE, HOMING_METHODS, sizeof(HOMING_METHODS) / sizeof(HOMING_METHODS[0])},
    {0x6099, 0x01, "Search speed for the switch", Control::NUMBER, nullptr, 0},
    {0x6099, 0x02, "Search speed for zero", Control::NUMBER, nullptr, 0},
    {0x609A, 0x00, "Homing acceleration", Control::NUMBER, nullptr, 0},
    {0x2005, 0x1C, "Homing time limit", Control::NUMBER, nullptr, 0},
};

static const SetupItem SUPPLY_ITEMS[] = {
    {0x2001, 0x10, "Undervoltage limit", Control::SHOW, nullptr, 0},
    {0x2001, 0x0E, "Overvoltage limit", Control::NUMBER, nullptr, 0},
};

static const SetupItem DANGER_ITEMS[] = {
    {0x200C, 0x02, "Node ID", Control::BUS, nullptr, 0},
    {0x200C, 0x03, "Bitrate", Control::BUS, BITRATES, sizeof(BITRATES) / sizeof(BITRATES[0])},
    {0x1011, 0x01, "Factory defaults", Control::RESTORE, nullptr, 0},
};

#define SECTION(title, subtitle, items) {title, subtitle, items, sizeof(items) / sizeof(items[0])}

static const SetupSection SETUP_SECTIONS[] = {
    SECTION("Current limits", "Rated and maximum current with its time set the overload and stall detection.", LIMIT_ITEMS),
    SECTION("Speeds and ramps", "The profile ramps are set on the Velocity and Position screens.", RAMP_ITEMS),
    SECTION("Fault reactions", "What the drive does on each alarm, and whether the alarm clears itself.", REACTION_ITEMS),
    SECTION("Inputs", "Two inputs with the same function raise a parameter fault. The PTU uses DI1 as home switch.", INPUT_ITEMS),
    SECTION("Homing", "The PTU requires method 21 with the home switch on DI1.", HOMING_ITEMS),
    SECTION("Supply", "", SUPPLY_ITEMS),
    SECTION("Bus and factory reset", "A PTU needs node 1 at 1 Mbit/s. Change one of the two at a time, so the console finds the motor after the power cycle.", DANGER_ITEMS),
};

// The Tuning tab (PAR-009): the PTU tunes only the speed loop gain and integral time

static const SetupItem SPEED_LOOP_ITEMS[] = {
    {0x2008, 0x01, "Gain", Control::GAIN, nullptr, 0},
    {0x2008, 0x02, "Integral time", Control::GAIN, nullptr, 0},
    {0x2008, 0x14, "Feedback filter cutoff", Control::NUMBER, nullptr, 0},
};

static const SetupItem POSITION_LOOP_ITEMS[] = {
    {0x2008, 0x03, "Gain", Control::GAIN, nullptr, 0},
    {0x2008, 0x10, "Speed feed-forward", Control::NUMBER, nullptr, 0},
    {0x2008, 0x0F, "Feed-forward filter", Control::NUMBER, nullptr, 0},
    {0x2008, 0x12, "Torque feed-forward", Control::NUMBER, nullptr, 0},
};

static const SetupItem FILTER_ITEMS[] = {
    {0x2007, 0x06, "Torque command filter", Control::NUMBER, nullptr, 0},
    {0x2009, 0x0D, "Notch 1 frequency", Control::NUMBER, nullptr, 0},
    {0x2009, 0x0E, "Notch 1 width", Control::NUMBER, nullptr, 0},
    {0x2009, 0x0F, "Notch 1 depth", Control::NUMBER, nullptr, 0},
    {0x2009, 0x10, "Notch 2 frequency", Control::NUMBER, nullptr, 0},
    {0x2009, 0x11, "Notch 2 width", Control::NUMBER, nullptr, 0},
    {0x2009, 0x12, "Notch 2 depth", Control::NUMBER, nullptr, 0},
    {0x2009, 0x13, "Notch 3 frequency", Control::NUMBER, nullptr, 0},
    {0x2009, 0x14, "Notch 3 width", Control::NUMBER, nullptr, 0},
    {0x2009, 0x15, "Notch 3 depth", Control::NUMBER, nullptr, 0},
    {0x2009, 0x16, "Notch 4 frequency", Control::NUMBER, nullptr, 0},
    {0x2009, 0x17, "Notch 4 width", Control::NUMBER, nullptr, 0},
    {0x2009, 0x18, "Notch 4 depth", Control::NUMBER, nullptr, 0},
};

static const SetupItem GAIN_SWITCH_ITEMS[] = {
    {0x2008, 0x04, "Second speed gain", Control::NUMBER, nullptr, 0},
    {0x2008, 0x05, "Second integral time", Control::NUMBER, nullptr, 0},
    {0x2008, 0x06, "Second position gain", Control::NUMBER, nullptr, 0},
    {0x2008, 0x08, "Mode", Control::NUMBER, nullptr, 0},
    {0x2008, 0x09, "Condition", Control::NUMBER, nullptr, 0},
    {0x2008, 0x0A, "Delay", Control::NUMBER, nullptr, 0},
    {0x2008, 0x0B, "Level", Control::NUMBER, nullptr, 0},
    {0x2008, 0x0C, "Hysteresis", Control::NUMBER, nullptr, 0},
};

static const SetupItem STEP_TEST_ITEMS[] = {
    {0, static_cast<uint8_t>(StepTestValue::VELOCITY_A), "Speed A", Control::TEST, nullptr, 0},
    {0, static_cast<uint8_t>(StepTestValue::VELOCITY_B), "Speed B", Control::TEST, nullptr, 0},
    {0, static_cast<uint8_t>(StepTestValue::STEP), "Distance there and back", Control::TEST, nullptr, 0},
    {0, static_cast<uint8_t>(StepTestValue::MOVE_SPEED), "Move speed", Control::TEST, nullptr, 0},
    {0, static_cast<uint8_t>(StepTestValue::ACCELERATION), "Ramp", Control::TEST, nullptr, 0},
    {0, static_cast<uint8_t>(StepTestValue::DWELL), "Time at each target", Control::TEST, nullptr, 0},
};

static const SetupSection TUNING_SECTIONS[] = {
    SECTION("Step test", "Console settings, used from the next step.", STEP_TEST_ITEMS),
    SECTION("Speed loop", "Higher gain and shorter integral time respond faster, until the motor hums or overshoots.", SPEED_LOOP_ITEMS),
    SECTION("Position loop", "", POSITION_LOOP_ITEMS),
    SECTION("Filters", "", FILTER_ITEMS),
    SECTION("Gain switching", "", GAIN_SWITCH_ITEMS),
};

static constexpr int32_t ROW_MIN_HEIGHT = 64;
static constexpr int32_t VALUE_BUTTON_WIDTH = 320;
static constexpr int32_t TUNING_VALUE_BUTTON_WIDTH = 170;
static constexpr int32_t STEPPER_WIDTH = 64;
static constexpr uint32_t STEP_PERCENT = 10;
static constexpr int32_t SELF_RESET_BUTTON_WIDTH = 190;

// Helpers

static lv_obj_t* addLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, Theme::color(color), 0);

    return label;
}

static lv_obj_t* createBox(lv_obj_t* parent, lv_flex_flow_t flow) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, flow);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    return box;
}

static const char* findOptionLabel(const PickerOption* options, size_t count, int32_t value) {
    for (size_t index = 0; index < count; index++) {
        if (options[index].value == value) {
            return options[index].label;
        }
    }

    return nullptr;
}

static const UnsavedChange* findUnsaved(const SetupListState* state, size_t position) {
    for (size_t index = 0; index < state->unsavedCount; index++) {
        if (state->unsaved[index].position == position) {
            return &state->unsaved[index];
        }
    }

    return nullptr;
}

/**
 * Formats a setting's value: its option name for choices and reactions, otherwise in its unit.
 *
 * @param state List state
 * @param item The setting
 * @param entry Its table entry
 * @param raw Its value
 * @param buffer Receives the text
 * @param bufferSize Size of the buffer
 */
static void formatItemValue(const SetupListState* state, const SetupItem& item, const Parameters::Entry& entry, uint32_t raw, char* buffer, size_t bufferSize) {
    if (item.control == Control::REACTION) {
        const char* label = findOptionLabel(REACTIONS, sizeof(REACTIONS) / sizeof(REACTIONS[0]), AlarmWord::getResponseCode(raw));
        snprintf(buffer, bufferSize, "%s", label != nullptr ? label : "Unknown reaction");

        return;
    }

    if (item.control == Control::CHOICE || (item.control == Control::BUS && item.options != nullptr)) {
        int32_t value = static_cast<int32_t>(ParameterFormat::toValue(entry, raw));
        const char* label = findOptionLabel(item.options, item.optionCount, value);

        if (label != nullptr) {
            snprintf(buffer, bufferSize, "%s", label);
        } else {
            snprintf(buffer, bufferSize, "%" PRId32 " (not listed)", value);
        }

        return;
    }

    ParameterFormat::formatValue(entry, raw, state->unitsPerTurn, buffer, bufferSize);
}

// Editing

static void onPicked(int32_t value, void* userData) {
    PickTarget* target = static_cast<PickTarget*>(userData);
    const Parameters::Entry& entry = Parameters::ENTRIES[target->position];
    uint32_t raw = static_cast<uint32_t>(value);

    // A reaction word: change one byte, keep the alarm code
    if (entry.index == 0x200E) {
        if (target->isSelfReset) {
            raw = (target->word & 0xFF00FFFFu) | (static_cast<uint32_t>(value) << 16);
        } else {
            raw = (target->word & 0x00FFFFFFu) | (static_cast<uint32_t>(value) << 24);
        }
    }

    target->context->motor.requestWriteParameter(target->position, raw);
}

static void onBusConfirmed(void* userData) {
    BusTarget* target = static_cast<BusTarget*>(userData);
    target->context->motor.requestWriteParameter(target->position, target->raw);
}

/**
 * Asks before a node ID or bitrate write: where the motor will answer, the power cycle, and the PTU's setting.
 *
 * @param value The new node ID, or the new bitrate code
 * @param userData BusTarget, with the context and position set
 */
static void confirmBusSetting(int32_t value, void* userData) {
    static char title[48];
    static char firstPoint[160];
    static char writes[48];
    static const char* points[4];
    BusTarget* target = static_cast<BusTarget*>(userData);
    const Parameters::Entry& entry = Parameters::ENTRIES[target->position];
    bool isNodeId = entry.subIndex == 0x02;
    target->raw = static_cast<uint32_t>(value);

    if (isNodeId) {
        snprintf(title, sizeof(title), "Move the motor to node %" PRId32 "?", value);
        snprintf(firstPoint, sizeof(firstPoint), "The motor keeps answering at its current node until it is power-cycled, then only at node %" PRId32 ".", value);
    } else {
        const char* name = findOptionLabel(BITRATES, sizeof(BITRATES) / sizeof(BITRATES[0]), value);
        snprintf(title, sizeof(title), "Change the bitrate to %s?", name != nullptr ? name : "?");
        snprintf(firstPoint, sizeof(firstPoint), "The motor keeps its current bitrate until it is power-cycled, then answers only at %s.", name != nullptr ? name : "?");
    }

    snprintf(writes, sizeof(writes), "%04X:%02X = %" PRId32, entry.index, entry.subIndex, value);
    points[0] = "Then SAVE: without a save the drive comes back with the old setting after the power cycle.";
    points[1] = firstPoint;
    points[2] = "After the power cycle the console searches for the motor and shows where it found it.";
    points[3] = "A motor going back into a PTU needs node 1 at 1 Mbit/s.";

    ConfirmDialogOptions options;
    options.title = title;
    options.subtitle = "Node ID and bitrate decide where the motor answers on the bus.";
    options.points = points;
    options.pointCount = sizeof(points) / sizeof(points[0]);
    options.writes = writes;
    options.confirmText = "CHANGE";
    options.onConfirm = onBusConfirmed;
    options.userData = target;
    openConfirmDialog(target->context->navigator.getOverlayLayer(), options);
}

/**
 * Opens the editor for a node ID or bitrate: the number pad or the bitrate list, then the confirm.
 *
 * @param state List state
 * @param item The setting
 * @param position Its entry position
 */
static void openBusEditor(SetupListState* state, const SetupItem& item, size_t position) {
    uint32_t raw = state->values[position].raw;
    lv_obj_t* overlay = state->context->navigator.getOverlayLayer();
    busTarget.context = state->context;
    busTarget.position = position;

    if (item.options == nullptr) {
        NumberPadOptions options;
        options.title = item.label;
        options.value = static_cast<int32_t>(raw);
        options.min = 1;
        options.max = 127; // CANopen node IDs; the drive's table allows up to 247
        options.onConfirm = confirmBusSetting;
        options.userData = &busTarget;
        openNumberPad(overlay, options);

        return;
    }

    OptionPickerOptions options;
    options.title = item.label;
    options.subtitle = "Applies after a power cycle";
    options.options = item.options;
    options.optionCount = item.optionCount;
    options.current = static_cast<int32_t>(raw);
    options.onPick = confirmBusSetting;
    options.userData = &busTarget;
    openOptionPicker(overlay, options);
}

/**
 * Opens the editor for a setting: a picker for choices and reactions, the number pad otherwise.
 *
 * @param state List state
 * @param item The setting
 * @param position Its entry position
 * @param isSelfReset For a reaction: edit the self-reset byte rather than the reaction code
 */
static void openEditor(SetupListState* state, const SetupItem& item, size_t position, bool isSelfReset) {
    uint32_t raw = state->values[position].raw;

    if (item.control == Control::NUMBER || item.control == Control::GAIN) {
        openParameterEditor(*state->context, position, raw, item.label);

        return;
    }

    if (item.control == Control::BUS) {
        openBusEditor(state, item, position);

        return;
    }

    pickTarget.context = state->context;
    pickTarget.position = position;
    pickTarget.word = raw;
    pickTarget.isSelfReset = isSelfReset;

    OptionPickerOptions options;
    options.title = item.label;
    options.onPick = onPicked;
    options.userData = &pickTarget;

    if (item.control == Control::REACTION && isSelfReset) {
        options.subtitle = "Whether the alarm goes away on its own once its cause has";
        options.options = SELF_RESET;
        options.optionCount = sizeof(SELF_RESET) / sizeof(SELF_RESET[0]);
        options.current = AlarmWord::isSelfReset(raw) ? 1 : 0;
    } else if (item.control == Control::REACTION) {
        options.subtitle = "What the drive does when this alarm comes";
        options.options = REACTIONS;
        options.optionCount = sizeof(REACTIONS) / sizeof(REACTIONS[0]);
        options.current = AlarmWord::getResponseCode(raw);
    } else {
        options.options = item.options;
        options.optionCount = item.optionCount;
        options.current = static_cast<int32_t>(ParameterFormat::toValue(Parameters::ENTRIES[position], raw));
    }

    openOptionPicker(state->context->navigator.getOverlayLayer(), options);
}

static void onValueClicked(lv_event_t* event) {
    SetupListState* state = static_cast<SetupListState*>(lv_event_get_user_data(event));
    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));

    // The button's user data packs the section, the item and which part of a reaction it edits
    size_t section = (code >> 16) & 0xFF;
    size_t item = (code >> 8) & 0xFF;
    bool isSelfReset = (code & 0x01) != 0;
    const SetupItem& setupItem = state->sections[section].items[item];
    const Parameters::Entry* entry = Parameters::find(setupItem.index, setupItem.subIndex);

    if (entry != nullptr) {
        openEditor(state, setupItem, static_cast<size_t>(entry - Parameters::ENTRIES), isSelfReset);
    }
}

/**
 * Steps a gain by about 10 % within its range. A step that would cross the value from before the first change stops
 * there, so stepping back always finds it (10 % steps, rounded, don't always come back by themselves: 1500 -> 1364 ->
 * 1500 does, 5 -> 5 -> 6 doesn't).
 *
 * @param entry Table entry
 * @param raw Current value
 * @param originalRaw The value before the first unsaved change, or the current one
 * @param isUp True to step up
 * @return The stepped value
 */
static uint32_t stepGain(const Parameters::Entry& entry, uint32_t raw, uint32_t originalRaw, bool isUp) {
    int64_t value = static_cast<int64_t>(raw);
    int64_t original = static_cast<int64_t>(originalRaw);

    // Up multiplies by 1.1 and down divides by it, rounded; at least one unit
    int64_t stepped = isUp ? (value * (100 + STEP_PERCENT) + 50) / 100 : (value * 100 + (100 + STEP_PERCENT) / 2) / (100 + STEP_PERCENT);

    if (stepped == value) {
        stepped = isUp ? value + 1 : value - 1;
    }

    // Stop at the original value on the way past it
    if (isUp && value < original && stepped > original) {
        stepped = original;
    } else if (!isUp && value > original && stepped < original) {
        stepped = original;
    }

    if (entry.hasRange) {
        stepped = std::clamp<int64_t>(stepped, entry.rangeMin, entry.rangeMax);
    }

    return static_cast<uint32_t>(std::max<int64_t>(stepped, 0));
}

static void onStepClicked(lv_event_t* event) {
    SetupListState* state = static_cast<SetupListState*>(lv_event_get_user_data(event));
    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    uintptr_t code = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
    const SetupItem& item = state->sections[(code >> 16) & 0xFF].items[(code >> 8) & 0xFF];
    const Parameters::Entry* entry = Parameters::find(item.index, item.subIndex);

    if (entry == nullptr) {
        return;
    }

    // From the last value asked for, so quick taps add up before the drive's read-back rebuilds the list
    size_t position = static_cast<size_t>(entry - Parameters::ENTRIES);
    const UnsavedChange* unsaved = findUnsaved(state, position);
    uint32_t originalRaw = unsaved != nullptr ? unsaved->before : state->values[position].raw;
    uint32_t raw = stepGain(*entry, state->values[position].raw, originalRaw, (code & 0x04) != 0);
    state->values[position].raw = raw;
    state->context->motor.requestWriteParameter(position, raw);

    // The value button sits between the steppers
    char text[48];
    ParameterFormat::formatValue(*entry, raw, state->unitsPerTurn, text, sizeof(text));
    lv_obj_t* valueButton = lv_obj_get_child(lv_obj_get_parent(button), 2);
    lv_obj_t* label = lv_obj_get_child(valueButton, 0);

    if (label != nullptr && lv_obj_check_type(label, &lv_label_class)) {
        lv_label_set_text(label, text);
    }
}

// Rows

/**
 * Adds a value button: the value as its label, tapping edits it.
 *
 * @param state List state
 * @param parent The row
 * @param text The value
 * @param code Section, item and part, packed (see onValueClicked)
 * @param width Button width
 * @param isEnabled False while the motor runs and the setting applies only when stopped
 * @return The button
 */
static lv_obj_t* addValueButton(SetupListState* state, lv_obj_t* parent, const char* text, uintptr_t code, int32_t width, bool isEnabled) {
    ButtonOptions options;
    options.text = text;
    options.variant = ButtonVariant::SECONDARY_OUTLINE;
    options.isCompact = true;
    options.onClick = onValueClicked;
    options.userData = state;
    lv_obj_t* button = createButton(parent, options);
    lv_obj_set_width(button, width);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(code));
    setButtonEnabled(button, isEnabled);

    return button;
}

static void onTestValueClicked(lv_event_t* event) {
    SetupListState* state = static_cast<SetupListState*>(lv_event_get_user_data(event));
    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    StepTestValue value = static_cast<StepTestValue>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
    openStepTestValueEditor(*state->context, value);
}

/**
 * Adds a step test setting's row: its name and a value button opening the number pad. Settings the chosen kind of step
 * doesn't use aren't shown.
 *
 * @param state List state
 * @param parent The list
 * @param item The setting
 */
static void addTestRow(SetupListState* state, lv_obj_t* parent, const SetupItem& item) {
    StepTestValue value = static_cast<StepTestValue>(item.subIndex);
    UiState::Motor& motor = state->context->state.motor;

    if (!isStepTestValueShown(motor, value)) {
        return;
    }

    lv_obj_t* row = createBox(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_min_height(row, ROW_MIN_HEIGHT, 0);
    lv_obj_set_style_pad_ver(row, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, Theme::color(Theme::BORDER), 0);

    lv_obj_t* name = addLabel(row, item.label, Theme::FONT_BODY, Theme::TEXT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);

    char text[32];
    formatStepTestValue(motor, value, text, sizeof(text));

    ButtonOptions options;
    options.text = text;
    options.variant = ButtonVariant::SECONDARY_OUTLINE;
    options.isCompact = true;
    options.onClick = onTestValueClicked;
    options.userData = state;
    lv_obj_t* button = createButton(row, options);
    lv_obj_set_width(button, state->valueWidth);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<uintptr_t>(value)));
}

static void onRestoreClicked(lv_event_t* event) {
    openRestoreDefaultsConfirm(*static_cast<SetupListState*>(lv_event_get_user_data(event))->context);
}

/**
 * Tells whether the drive's power stage is on: the bus settings and restore wait until it is off.
 *
 * @param state List state
 * @return True in Operation enabled or Quick stop active
 */
static bool isPowered(const SetupListState* state) {
    DriveState driveState = static_cast<DriveState>(lv_subject_get_int(&state->context->state.motor.state));

    return driveState == DriveState::OPERATION_ENABLED || driveState == DriveState::QUICK_STOP_ACTIVE;
}

static void addRestoreRow(SetupListState* state, lv_obj_t* parent, const SetupItem& item) {
    lv_obj_t* row = createBox(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_min_height(row, ROW_MIN_HEIGHT, 0);
    lv_obj_set_style_pad_ver(row, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* left = createBox(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(left, 1);
    addLabel(left, item.label, Theme::FONT_BODY, Theme::TEXT);
    lv_obj_t* note = addLabel(left, "Every setting back to factory; also on the Faults screen", Theme::FONT_BODY_SMALL, Theme::TEXT_MUTED);
    lv_obj_set_width(note, lv_pct(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);

    ButtonOptions options;
    options.text = "FACTORY RESET";
    options.variant = ButtonVariant::DANGER_OUTLINE;
    options.isCompact = true;
    options.onClick = onRestoreClicked;
    options.userData = state;
    lv_obj_t* button = createButton(row, options);
    lv_obj_set_width(button, VALUE_BUTTON_WIDTH);
    bool isRunning = lv_subject_get_int(&state->context->state.motor.isRunning) != 0;
    setButtonEnabled(button, !isRunning && !isPowered(state));
}

static void addItemRow(SetupListState* state, lv_obj_t* parent, size_t sectionIndex, size_t itemIndex) {
    const SetupItem& item = state->sections[sectionIndex].items[itemIndex];

    if (item.control == Control::RESTORE) {
        addRestoreRow(state, parent, item);

        return;
    }

    if (item.control == Control::TEST) {
        addTestRow(state, parent, item);

        return;
    }

    const Parameters::Entry* entry = Parameters::find(item.index, item.subIndex);

    // A setting the drive doesn't have isn't shown
    if (entry == nullptr) {
        return;
    }

    size_t position = static_cast<size_t>(entry - Parameters::ENTRIES);
    const ParameterValue& value = state->values[position];
    bool isRunning = lv_subject_get_int(&state->context->state.motor.isRunning) != 0;
    bool canEdit = item.control != Control::SHOW && ParameterFormat::isEditable(*entry) && (!isRunning || ParameterFormat::isEditableWhileRunning(*entry));

    // Node ID and bitrate aren't editable in the table sense; the danger group changes them with the stage off
    if (item.control == Control::BUS) {
        canEdit = !isRunning && !isPowered(state);
    }
    char text[96];
    char note[192] = "";
    uint32_t noteColor = Theme::TEXT_MUTED;

    lv_obj_t* row = createBox(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_min_height(row, ROW_MIN_HEIGHT, 0);
    lv_obj_set_style_pad_ver(row, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, Theme::color(Theme::BORDER), 0);

    // Left: the name and a note
    lv_obj_t* left = createBox(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_t* name = addLabel(left, item.label != nullptr ? item.label : entry->description, Theme::FONT_BODY, Theme::TEXT);
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);

    if (!value.isRead || value.result != SdoResult::OK) {
        addLabel(left, value.isRead ? "read failed" : "not read yet", Theme::FONT_BODY_SMALL, value.isRead ? Theme::DANGER : Theme::TEXT_MUTED);

        return;
    }

    // The note: unsaved, the console's own value, a difference from factory, or why it can't change now
    const UnsavedChange* unsaved = findUnsaved(state, position);

    if (unsaved != nullptr && item.control == Control::BUS) {
        formatItemValue(state, item, *entry, unsaved->before, text, sizeof(text));
        snprintf(note, sizeof(note), "unsaved, was %s: SAVE, then power-cycle the motor", text);
        noteColor = Theme::ACCENT;
    } else if (unsaved != nullptr) {
        formatItemValue(state, item, *entry, unsaved->before, text, sizeof(text));
        snprintf(note, sizeof(note), "unsaved, was %s", text);
        noteColor = Theme::ACCENT;
    } else if ((entry->flags & Parameters::Flag::SESSION) != 0) {
        if (state->hasEarlierSessionValues || !value.hasOwnValue) {
            snprintf(note, sizeof(note), "set by the console for this session");
        } else {
            formatItemValue(state, item, *entry, value.ownRaw, text, sizeof(text));
            snprintf(note, sizeof(note), "set by the console for this session; the motor's own: %s", text);
        }
    } else if (entry->hasFactory && ParameterFormat::differsFromFactory(*entry, value.raw)) {
        formatItemValue(state, item, *entry, static_cast<uint32_t>(entry->factory), text, sizeof(text));
        snprintf(note, sizeof(note), "factory: %s", text);
        noteColor = Theme::ACCENT;
    } else if (isRunning && !canEdit && item.control != Control::SHOW) {
        snprintf(note, sizeof(note), "stop the motor to change");
    }

    if (note[0] != '\0') {
        lv_obj_t* noteLabel = addLabel(left, note, Theme::FONT_BODY_SMALL, noteColor);
        lv_obj_set_width(noteLabel, lv_pct(100));
        lv_label_set_long_mode(noteLabel, LV_LABEL_LONG_DOT);
    }

    // Right: the value, as a button where it can change
    formatItemValue(state, item, *entry, value.raw, text, sizeof(text));
    uintptr_t code = (static_cast<uintptr_t>(sectionIndex) << 16) | (static_cast<uintptr_t>(itemIndex) << 8);

    if (item.control == Control::SHOW) {
        addLabel(row, text, Theme::FONT_BODY, Theme::TEXT);

        return;
    }

    // A gain: - value +
    if (item.control == Control::GAIN) {
        ButtonOptions stepOptions;
        stepOptions.icon = Icons::REMOVE;
        stepOptions.variant = ButtonVariant::SECONDARY_OUTLINE;
        stepOptions.isCompact = true;
        stepOptions.onClick = onStepClicked;
        stepOptions.userData = state;
        lv_obj_t* minus = createButton(row, stepOptions);
        lv_obj_set_width(minus, STEPPER_WIDTH);
        lv_obj_set_user_data(minus, reinterpret_cast<void*>(code | 0x02));
        setButtonEnabled(minus, canEdit);

        addValueButton(state, row, text, code, state->valueWidth, canEdit);

        stepOptions.icon = Icons::ADD;
        lv_obj_t* plus = createButton(row, stepOptions);
        lv_obj_set_width(plus, STEPPER_WIDTH);
        lv_obj_set_user_data(plus, reinterpret_cast<void*>(code | 0x04));
        setButtonEnabled(plus, canEdit);

        return;
    }

    addValueButton(state, row, text, code, state->valueWidth, canEdit);

    // A reaction also has its self-reset as a second button
    if (item.control == Control::REACTION) {
        addValueButton(state, row, AlarmWord::isSelfReset(value.raw) ? "Clears itself" : "Needs a reset", code | 0x01, SELF_RESET_BUTTON_WIDTH, canEdit);
    }
}

static void rebuild(SetupListState* state) {
    int64_t startUs = esp_timer_get_time();
    MotorStatus status = state->context->motor.getStatus();
    state->context->motor.getParameterValues(state->values, Parameters::ENTRY_COUNT);
    state->unsavedCount = state->context->motor.getUnsavedChanges(state->unsaved, MotorService::MAX_UNSAVED_CHANGES);
    state->unitsPerTurn = status.unitsPerTurn;
    state->hasEarlierSessionValues = status.hasEarlierSessionValues;

    int32_t scrollY = lv_obj_get_scroll_y(state->list);
    lv_obj_clean(state->list);

    if (!status.isConnected) {
        addLabel(state->list, "No motor connected", Theme::FONT_BODY, Theme::TEXT_MUTED);

        return;
    }

    for (size_t sectionIndex = 0; sectionIndex < state->sectionCount; sectionIndex++) {
        const SetupSection& section = state->sections[sectionIndex];

        lv_obj_t* heading = addLabel(state->list, section.title, Theme::FONT_TITLE_SMALL, Theme::TEXT);
        lv_obj_set_style_pad_top(heading, sectionIndex == 0 ? 0 : Theme::SPACE_L, 0);

        if (section.subtitle[0] != '\0') {
            lv_obj_t* subtitle = addLabel(state->list, section.subtitle, Theme::FONT_BODY_SMALL, Theme::TEXT_MUTED);
            lv_obj_set_width(subtitle, lv_pct(100));
            lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_pad_bottom(subtitle, Theme::SPACE_XS, 0);
        }

        for (size_t itemIndex = 0; itemIndex < section.itemCount; itemIndex++) {
            addItemRow(state, state->list, sectionIndex, itemIndex);
        }
    }

    lv_obj_update_layout(state->list);
    lv_obj_scroll_to_y(state->list, scrollY, LV_ANIM_OFF);
    ESP_LOGI("SetupList", "Built in %" PRId64 " ms", (esp_timer_get_time() - startUs) / 1000);
}

// Observers: the app-lifetime context as user data; the list's state from the target

static void onChanged(lv_observer_t* observer, lv_subject_t* subject) {
    auto* state = static_cast<SetupListState*>(lv_obj_get_user_data(lv_observer_get_target_obj(observer)));

    // A full rebuild takes long with every value read; ten of them on opening tripped the task watchdog
    if (state->isCreating) {
        return;
    }

    rebuild(state);
}

static void onConnectedChanged(lv_observer_t* observer, lv_subject_t* subject) {
    // Read on opening and whenever a drive connects
    if (lv_subject_get_int(subject)) {
        static_cast<UiContext*>(lv_observer_get_user_data(observer))->motor.requestReadParameters();
    }
}

static void onListDeleted(lv_event_t* event) {
    delete static_cast<SetupListState*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(event))));
}

/**
 * Creates a list of settings from a section table.
 *
 * @param parent Parent object (a flex column)
 * @param context Motor service, state and navigation
 * @param sections The sections; static
 * @param sectionCount Number of sections
 * @param valueWidth Width of the value buttons
 * @return The list
 */
static lv_obj_t* createList(lv_obj_t* parent, UiContext& context, const SetupSection* sections, size_t sectionCount, int32_t valueWidth) {
    UiState::Motor& motor = context.state.motor;
    auto* state = new SetupListState();
    state->context = &context;
    state->sections = sections;
    state->sectionCount = sectionCount;
    state->valueWidth = valueWidth;

    // Fills the rest and scrolls (stays clickable, or presses go past it and nothing scrolls)
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_add_style(list, &Theme::scrollbarStyle, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(list, Theme::SCROLLBAR_WIDTH + Theme::SPACE_S, 0);
    lv_obj_set_user_data(list, state);
    lv_obj_add_event_cb(list, onListDeleted, LV_EVENT_DELETE, nullptr);
    state->list = list;

    // Rebuilt after reads and writes, and when running starts or stops or the stage switches (which settings can change)
    lv_subject_add_observer_obj(&motor.parametersVersion, onChanged, list, &context);
    lv_subject_add_observer_obj(&motor.isRunning, onChanged, list, &context);
    lv_subject_add_observer_obj(&motor.state, onChanged, list, &context);

    // The step test settings (Tuning list); rare changes, so a rebuild is fine
    lv_subject_t* testSubjects[] = {&motor.isStepTestPosition,
                                    &motor.stepTestVelocityARpm,
                                    &motor.stepTestVelocityBRpm,
                                    &motor.stepTestStepMilliturns,
                                    &motor.stepTestMoveSpeedRpm,
                                    &motor.stepTestAccelerationRpmPerSecond,
                                    &motor.stepTestDwellMs};

    for (lv_subject_t* subject : testSubjects) {
        lv_subject_add_observer_obj(subject, onChanged, list, &context);
    }
    lv_subject_add_observer_obj(&motor.isConnected, onConnectedChanged, list, &context);

    // Built once, now that every observer is in place
    state->isCreating = false;
    rebuild(state);

    return list;
}

lv_obj_t* createSetupList(lv_obj_t* parent, UiContext& context) {
    return createList(parent, context, SETUP_SECTIONS, sizeof(SETUP_SECTIONS) / sizeof(SETUP_SECTIONS[0]), VALUE_BUTTON_WIDTH);
}

lv_obj_t* createTuningList(lv_obj_t* parent, UiContext& context) {
    return createList(parent, context, TUNING_SECTIONS, sizeof(TUNING_SECTIONS) / sizeof(TUNING_SECTIONS[0]), TUNING_VALUE_BUTTON_WIDTH);
}
