#include "ParameterList.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "protocol/ParameterFormat.hpp"
#include "services/MotorService.hpp"
#include "ui/Bindings.hpp"
#include "ui/Navigator.hpp"
#include "ui/ParameterEditor.hpp"
#include "ui/Theme.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/Dialog.hpp"
#include "ui/fonts/Icons.hpp"

namespace {
/**
 * What the list keeps between rebuilds; freed with the list.
 */
struct ParameterListState {
    UiContext* context = nullptr;
    lv_obj_t* groups = nullptr;                     // Scrolling column of group sections
    ParameterValue values[Parameters::ENTRY_COUNT]; // Copy of the service's values, refreshed per read
    bool isExpanded[Parameters::GROUP_COUNT] = {};  // Open groups, kept across reads
    int32_t unitsPerTurn = 0;                       // For position units
    bool hasEarlierSessionValues = false;           // The captured own values are the console's (PAR-002)

    // Filter (PAR-013)
    char filter[32] = "";                                // Lower case; empty when not filtering
    bool isFilterExpanded[Parameters::GROUP_COUNT] = {}; // Open groups while filtering; isExpanded is kept for after
    size_t shownRowCount = 0;                            // Rows built in this rebuild, against MAX_FILTER_ROWS
    lv_obj_t* filterBox = nullptr;                       // Text area
    lv_obj_t* keyboard = nullptr;                        // On-screen keyboard, hidden when closed
    lv_timer_t* filterTimer = nullptr;                   // Applies the filter once typing pauses

    // Unsaved changes (PAR-004)
    UnsavedChange unsaved[MotorService::MAX_UNSAVED_CHANGES];
    size_t unsavedCount = 0;
};

} // namespace

static constexpr int32_t ROW_MIN_HEIGHT = 56;
static constexpr int32_t VALUE_COLUMN_WIDTH = 300;
static constexpr int32_t DIALOG_WIDTH = 760;
static constexpr int32_t DETAIL_LABEL_WIDTH = 200;
static constexpr size_t MIN_FILTER_LENGTH = 2; // One character matches most of the list
static constexpr size_t MAX_FILTER_ROWS = 80;  // Rows built for a filter; more is slow and no help
static constexpr int32_t FILTER_BOX_WIDTH = 380;
static constexpr int32_t KEYBOARD_HEIGHT = 260;   // About half the content area, so matches show above it
static constexpr uint32_t FILTER_DELAY_MS = 1000; // After the last keystroke; each filter rebuilds the list

static ParameterListState* getState(lv_obj_t* object) {
    return static_cast<ParameterListState*>(lv_obj_get_user_data(object));
}

static bool isFiltering(const ParameterListState* state) {
    return state->filter[0] != '\0';
}

/**
 * Tells whether a text contains a lower-case needle, ignoring case.
 *
 * @param text Text to search
 * @param needle Lower-case text to find
 * @return True if found
 */
static bool containsIgnoringCase(const char* text, const char* needle) {
    for (const char* start = text; *start != '\0'; start++) {
        const char* letter = start;
        const char* wanted = needle;

        while (*letter != '\0' && *wanted != '\0' && tolower(static_cast<unsigned char>(*letter)) == *wanted) {
            letter++;
            wanted++;
        }

        if (*wanted == '\0') {
            return true;
        }
    }

    return false;
}

/**
 * Tells whether an entry matches the filter: its description, manual name, index ("2001", "2001:10") or a value name.
 *
 * @param state List state
 * @param entry Table entry
 * @return True if it matches, or no filter is set
 */
static bool matchesFilter(const ParameterListState* state, const Parameters::Entry& entry) {
    if (!isFiltering(state)) {
        return true;
    }

    char index[8];
    snprintf(index, sizeof(index), "%04X:%02X", entry.index, entry.subIndex);

    if (containsIgnoringCase(entry.description, state->filter) || containsIgnoringCase(entry.name, state->filter) || containsIgnoringCase(index, state->filter)) {
        return true;
    }

    for (size_t label = 0; label < entry.labelCount; label++) {
        if (containsIgnoringCase(entry.labels[label].label, state->filter)) {
            return true;
        }
    }

    return false;
}

// Labels

static lv_obj_t* addLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, Theme::color(color), 0);

    return label;
}

/**
 * Creates a plain container without a surface.
 *
 * @param parent Parent object
 * @param flow Flex flow
 * @return The container
 */
static lv_obj_t* createBox(lv_obj_t* parent, lv_flex_flow_t flow) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, flow);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

    return box;
}

/**
 * Tells whether a value counts as changed from factory: read, different, and something a user sets. The console's own
 * values have their own mark, and locked entries are motor constants and calibration, which differ per motor model
 * and drift (2000:1D), not settings.
 *
 * @param entry Table entry
 * @param value Value as read
 * @return True if the row and its group should show it as changed
 */
static bool isChangedFromFactory(const Parameters::Entry& entry, const ParameterValue& value) {
    uint8_t notSettings = Parameters::Flag::SESSION | Parameters::Flag::CONSOLE_OWNED | Parameters::Flag::LOCKED;

    return value.isRead && value.result == SdoResult::OK && (entry.flags & notSettings) == 0 && ParameterFormat::differsFromFactory(entry, value.raw);
}

/**
 * Finds a parameter's unsaved change.
 *
 * @param state List state
 * @param position Entry position
 * @return The change, or nullptr if the parameter wasn't written since the last save
 */
static const UnsavedChange* findUnsaved(const ParameterListState* state, size_t position) {
    for (size_t index = 0; index < state->unsavedCount; index++) {
        if (state->unsaved[index].position == position) {
            return &state->unsaved[index];
        }
    }

    return nullptr;
}

// Editing (PAR-004)

static void onEditClicked(lv_event_t* event) {
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));
    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    size_t position = reinterpret_cast<size_t>(lv_obj_get_user_data(button));

    // The details dialog closes; the pad takes its place
    closeDialog(button);
    openParameterEditor(*state->context, position, state->values[position].raw);
}

// Details

/**
 * Adds a "label: value" line to the details dialog.
 *
 * @param body Dialog body
 * @param name Label
 * @param value Value text
 * @param color Value colour token
 */
static void addDetail(lv_obj_t* body, const char* name, const char* value, uint32_t color) {
    lv_obj_t* row = createBox(body, LV_FLEX_FLOW_ROW);

    lv_obj_t* nameLabel = addLabel(row, name, Theme::FONT_BODY, Theme::TEXT_MUTED);
    lv_obj_set_width(nameLabel, DETAIL_LABEL_WIDTH);

    lv_obj_t* valueLabel = addLabel(row, value, Theme::FONT_BODY, color);
    lv_obj_set_flex_grow(valueLabel, 1);
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_WRAP);
}

static void openDetails(ParameterListState* state, size_t position) {
    const Parameters::Entry& entry = Parameters::ENTRIES[position];
    const ParameterValue& value = state->values[position];
    char title[96];
    char subtitle[96];
    char text[160];
    char converted[64];
    char raw[32];

    snprintf(title, sizeof(title), "%s", entry.description[0] != '\0' ? entry.description : entry.name);
    snprintf(subtitle, sizeof(subtitle), "%04X:%02X  %s  –  %s", entry.index, entry.subIndex, entry.name, Parameters::GROUPS[entry.group]);

    DialogOptions options;
    options.title = title;
    options.subtitle = subtitle;
    options.width = DIALOG_WIDTH;
    lv_obj_t* body = openDialog(state->context->navigator.getOverlayLayer(), options);

    // The value as read, converted and raw
    if (!value.isRead) {
        addDetail(body, "Value", entry.access == Parameters::Access::WRITE_ONLY ? "Write-only, can't be read" : "Not read", Theme::TEXT_MUTED);
    } else if (value.result != SdoResult::OK) {
        snprintf(text, sizeof(text), "%s", value.result == SdoResult::ABORTED ? SdoClient::getAbortCodeDescription(value.abortCode) : SdoClient::getResultName(value.result));
        addDetail(body, "Value", text, Theme::DANGER);
    } else {
        ParameterFormat::formatValue(entry, value.raw, state->unitsPerTurn, converted, sizeof(converted));
        ParameterFormat::formatRaw(entry, value.raw, raw, sizeof(raw));

        if (ParameterFormat::isConverted(entry)) {
            snprintf(text, sizeof(text), "%s  (raw %s)", converted, raw);
        } else {
            snprintf(text, sizeof(text), "%s", converted);
        }

        addDetail(body, "Value", text, isChangedFromFactory(entry, value) ? Theme::ACCENT : Theme::TEXT);
    }

    if (entry.hasFactory) {
        ParameterFormat::formatValue(entry, static_cast<uint32_t>(entry.factory), state->unitsPerTurn, converted, sizeof(converted));
        addDetail(body, "Factory", converted, Theme::TEXT);
    }

    // The console's own values (D-028)
    if ((entry.flags & Parameters::Flag::SESSION) != 0 || (entry.flags & Parameters::Flag::CONSOLE_OWNED) != 0) {
        const char* who = (entry.flags & Parameters::Flag::SESSION) != 0 ? "Set by the console for this session" : "Set by the motion screens on every start";

        if (state->hasEarlierSessionValues) {
            snprintf(text, sizeof(text), "%s. The motor's own value is unknown: it wasn't power-cycled since an earlier console session", who);
        } else if (value.hasOwnValue) {
            ParameterFormat::formatValue(entry, value.ownRaw, state->unitsPerTurn, converted, sizeof(converted));
            snprintf(text, sizeof(text), "%s. The motor's own value: %s", who, converted);
        } else {
            snprintf(text, sizeof(text), "%s", who);
        }

        addDetail(body, "Console", text, Theme::TEXT);
    }

    // What it is and when a change applies
    // A range that is just the data type's own limits says nothing
    if (entry.hasRange && !ParameterFormat::isFullTypeRange(entry)) {
        char minimum[48];
        char maximum[48];
        ParameterFormat::formatValue(entry, static_cast<uint32_t>(entry.rangeMin), state->unitsPerTurn, minimum, sizeof(minimum));
        ParameterFormat::formatValue(entry, static_cast<uint32_t>(entry.rangeMax), state->unitsPerTurn, maximum, sizeof(maximum));
        snprintf(text, sizeof(text), "%s to %s", minimum, maximum);
        addDetail(body, "Range", text, Theme::TEXT);
    }

    const char* access = entry.access == Parameters::Access::READ_WRITE ? "read and write" : entry.access == Parameters::Access::WRITE_ONLY ? "write only" : "read only";
    snprintf(text, sizeof(text), "%s, %s", ParameterFormat::getTypeText(entry.type), access);
    addDetail(body, "Type", text, Theme::TEXT);

    if (entry.access != Parameters::Access::READ_ONLY) {
        addDetail(body, "Applies", ParameterFormat::getEffectText(entry.effect), Theme::TEXT);
    }

    if ((entry.flags & Parameters::Flag::LOCKED) != 0) {
        addDetail(body, "Locked", entry.lockReason, Theme::WARNING);
    } else if ((entry.flags & Parameters::Flag::SETUP_ONLY) != 0) {
        addDetail(body, "Changed in", "Setup, behind a confirm (moves the motor to another node or bitrate)", Theme::WARNING);
    }

    // The value names, for enumerations
    // Written since the last save
    const UnsavedChange* unsaved = findUnsaved(state, position);

    if (unsaved != nullptr) {
        ParameterFormat::formatValue(entry, unsaved->before, state->unitsPerTurn, converted, sizeof(converted));
        snprintf(text, sizeof(text), "Changed from %s, not saved: gone at the motor's power-off unless saved", converted);
        addDetail(body, "Unsaved", text, Theme::ACCENT);
    }

    if (entry.labelCount > 0) {
        size_t length = 0;
        text[0] = '\0';

        for (size_t label = 0; label < entry.labelCount && length < sizeof(text); label++) {
            length += snprintf(text + length, sizeof(text) - length, "%s%" PRId32 " %s", label > 0 ? ", " : "", entry.labels[label].value, entry.labels[label].label);
        }

        addDetail(body, "Values", text, Theme::TEXT);
    }

    // EDIT for what a user may change; while the motor runs, only what applies at once (D5)
    if (!ParameterFormat::isEditable(entry) || !value.isRead || value.result != SdoResult::OK) {
        return;
    }

    bool isRunning = lv_subject_get_int(&state->context->state.motor.isRunning) != 0;
    bool canEdit = !isRunning || ParameterFormat::isEditableWhileRunning(entry);

    if (!canEdit) {
        addDetail(body, "Edit", "Stop the motor to change this: it applies when the motor stops", Theme::WARNING);
    }

    lv_obj_t* actions = addDialogActions(body);
    ButtonOptions editOptions;
    editOptions.text = "EDIT";
    editOptions.icon = Icons::PARAMETERS;
    editOptions.variant = ButtonVariant::PRIMARY;
    editOptions.onClick = onEditClicked;
    editOptions.userData = state;
    lv_obj_t* editButton = createButton(actions, editOptions);
    lv_obj_set_user_data(editButton, reinterpret_cast<void*>(position));
    setButtonEnabled(editButton, canEdit);
}

static void onRowClicked(lv_event_t* event) {
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));
    openDetails(state, reinterpret_cast<size_t>(lv_obj_get_user_data(row)));
}

// Rows

/**
 * Adds a parameter row: description and identifier on the left, value and its note on the right.
 *
 * @param state List state
 * @param parent The group's rows container
 * @param position Entry position in Parameters::ENTRIES
 */
static void addRow(ParameterListState* state, lv_obj_t* parent, size_t position) {
    const Parameters::Entry& entry = Parameters::ENTRIES[position];
    const ParameterValue& value = state->values[position];
    bool isLocked = (entry.flags & Parameters::Flag::LOCKED) != 0;
    bool isConsoleWritten = (entry.flags & (Parameters::Flag::SESSION | Parameters::Flag::CONSOLE_OWNED)) != 0;
    char text[96];
    char converted[64];

    lv_obj_t* row = createBox(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_min_height(row, ROW_MIN_HEIGHT, 0);
    lv_obj_set_style_pad_ver(row, Theme::SPACE_XS, 0);
    lv_obj_set_style_pad_column(row, Theme::SPACE_M, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, Theme::color(Theme::BORDER), 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(row, &Theme::pressedStyle, LV_STATE_PRESSED);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(position));
    lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_CLICKED, state);

    // Locked rows lead with a lock; directly in the row, since LVGL draws nesting recursively on a limited stack
    if (isLocked) {
        addLabel(row, Icons::LOCK, Theme::FONT_ICON_SMALL, Theme::TEXT_MUTED);
    }

    // Left: what it is, then its identifier
    lv_obj_t* left = createBox(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(left, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(left, 1);

    lv_obj_t* title = addLabel(left, entry.description[0] != '\0' ? entry.description : entry.name, Theme::FONT_BODY, isLocked ? Theme::TEXT_MUTED : Theme::TEXT);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    snprintf(text, sizeof(text), "%04X:%02X  %s", entry.index, entry.subIndex, entry.name);
    addLabel(left, text, Theme::FONT_CODE, Theme::TEXT_MUTED);

    // Right: the value, then a note: factory value, the console's own, locked or the read error
    lv_obj_t* right = createBox(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(right, VALUE_COLUMN_WIDTH);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    bool isChanged = isChangedFromFactory(entry, value);
    const char* note = nullptr;
    uint32_t noteColor = Theme::TEXT_MUTED;

    if (!value.isRead) {
        snprintf(converted, sizeof(converted), "%s", entry.access == Parameters::Access::WRITE_ONLY ? "write-only" : "–");
        addLabel(right, converted, Theme::FONT_BODY, Theme::TEXT_MUTED);
    } else if (value.result != SdoResult::OK) {
        addLabel(right, "read failed", Theme::FONT_BODY, Theme::DANGER);
        note = value.result == SdoResult::ABORTED ? SdoClient::getAbortCodeDescription(value.abortCode) : SdoClient::getResultName(value.result);
        noteColor = Theme::DANGER;
    } else {
        ParameterFormat::formatValue(entry, value.raw, state->unitsPerTurn, converted, sizeof(converted));
        addLabel(right, converted, Theme::FONT_BODY, isChanged ? Theme::ACCENT : Theme::TEXT);
    }

    const UnsavedChange* unsaved = findUnsaved(state, position);

    if (note == nullptr && unsaved != nullptr) {
        // Not saved yet; and when it isn't live yet, when it will be
        if (entry.effect == Parameters::Effect::WHEN_STOPPED) {
            note = "unsaved, applies when stopped";
        } else if (entry.effect == Parameters::Effect::AFTER_POWER_UP) {
            note = "unsaved, applies after power cycle";
        } else {
            note = "unsaved";
        }

        noteColor = Theme::ACCENT;
    } else if (note == nullptr && isConsoleWritten) {
        note = (entry.flags & Parameters::Flag::SESSION) != 0 ? "console session value" : "set by motion screens";
    } else if (note == nullptr && isChanged) {
        char factory[64];
        ParameterFormat::formatValue(entry, static_cast<uint32_t>(entry.factory), state->unitsPerTurn, factory, sizeof(factory));
        snprintf(text, sizeof(text), "factory %s", factory);
        note = text;
        noteColor = Theme::ACCENT;
    } else if (note == nullptr && isLocked) {
        note = "locked";
    }

    if (note != nullptr) {
        lv_obj_t* noteLabel = addLabel(right, note, Theme::FONT_BODY_SMALL, noteColor);
        lv_label_set_long_mode(noteLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_width(noteLabel, VALUE_COLUMN_WIDTH, 0);
    }
}

// Groups

/**
 * Fills or empties a group's rows container to match whether it is open, and points its chevron.
 *
 * @param state List state
 * @param section The group's section (header, then rows container)
 * @param group Group number
 */
static void showGroupRows(ParameterListState* state, lv_obj_t* section, size_t group) {
    lv_obj_t* header = lv_obj_get_child(section, 0);
    lv_obj_t* chevron = lv_obj_get_child(header, 0);
    lv_obj_t* rows = lv_obj_get_child(section, 1);
    bool isOpen = isFiltering(state) ? state->isFilterExpanded[group] : state->isExpanded[group];

    lv_label_set_text(chevron, isOpen ? Icons::EXPAND : Icons::CHEVRON_RIGHT);
    lv_obj_clean(rows);

    if (!isOpen) {
        return;
    }

    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[position];

        if (entry.group != group || !matchesFilter(state, entry)) {
            continue;
        }

        // A filter that matches much of the list builds only so many rows
        if (isFiltering(state) && state->shownRowCount >= MAX_FILTER_ROWS) {
            lv_obj_t* more = addLabel(rows, "More matches: type more to narrow it down", Theme::FONT_BODY_SMALL, Theme::TEXT_MUTED);
            lv_obj_set_style_pad_ver(more, Theme::SPACE_S, 0);

            return;
        }

        addRow(state, rows, position);
        state->shownRowCount++;
    }
}

static void onGroupClicked(lv_event_t* event) {
    lv_obj_t* header = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    lv_obj_t* section = lv_obj_get_parent(header);
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));
    size_t group = reinterpret_cast<size_t>(lv_obj_get_user_data(section));

    // Only the rows container is rebuilt, never the header that was tapped; while filtering, the filter's own open
    // groups change, so clearing the filter brings back the groups that were open before
    bool* expanded = isFiltering(state) ? state->isFilterExpanded : state->isExpanded;
    expanded[group] = !expanded[group];
    state->shownRowCount = 0;
    showGroupRows(state, section, group);
}

/**
 * Adds a group: a tappable header with its name, size and changed count, and a rows container.
 *
 * @param state List state
 * @param group Group number
 */
static void addGroup(ParameterListState* state, size_t group) {
    size_t entryCount = 0;
    size_t changedCount = 0;

    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[position];
        const ParameterValue& value = state->values[position];

        if (entry.group != group || !matchesFilter(state, entry)) {
            continue;
        }

        entryCount++;

        if (isChangedFromFactory(entry, value)) {
            changedCount++;
        }
    }

    // While filtering, groups without a match are left out
    if (entryCount == 0) {
        return;
    }

    lv_obj_t* section = createBox(state->groups, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_user_data(section, reinterpret_cast<void*>(group));

    lv_obj_t* header = createBox(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(header, Theme::TOUCH_TARGET_MIN);
    lv_obj_set_style_pad_column(header, Theme::SPACE_S, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, Theme::color(Theme::BORDER), 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(header, &Theme::pressedStyle, LV_STATE_PRESSED);
    lv_obj_add_event_cb(header, onGroupClicked, LV_EVENT_CLICKED, state);

    addLabel(header, Icons::CHEVRON_RIGHT, Theme::FONT_ICON_SMALL, Theme::TEXT_MUTED);

    lv_obj_t* name = addLabel(header, Parameters::GROUPS[group], Theme::FONT_TITLE_SMALL, Theme::TEXT);
    lv_obj_set_flex_grow(name, 1);

    if (changedCount > 0) {
        char changed[32];
        snprintf(changed, sizeof(changed), "%u changed", static_cast<unsigned>(changedCount));
        addLabel(header, changed, Theme::FONT_BODY_SMALL, Theme::ACCENT);
    }

    char count[16];
    snprintf(count, sizeof(count), "%u", static_cast<unsigned>(entryCount));
    addLabel(header, count, Theme::FONT_BODY_SMALL, Theme::TEXT_MUTED);

    lv_obj_t* rows = createBox(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(rows, Theme::SPACE_L, 0);
    showGroupRows(state, section, group);
}

/**
 * Rebuilds the groups from a fresh copy of the values, keeping open groups and the scroll position.
 *
 * @param state List state
 */
static void rebuild(ParameterListState* state) {
    MotorStatus status = state->context->motor.getStatus();
    state->context->motor.getParameterValues(state->values, Parameters::ENTRY_COUNT);
    state->unitsPerTurn = status.unitsPerTurn;
    state->hasEarlierSessionValues = status.hasEarlierSessionValues;
    state->unsavedCount = state->context->motor.getUnsavedChanges(state->unsaved, MotorService::MAX_UNSAVED_CHANGES);

    int32_t scrollY = lv_obj_get_scroll_y(state->groups);
    lv_obj_clean(state->groups);
    state->shownRowCount = 0;

    for (size_t group = 0; group < Parameters::GROUP_COUNT; group++) {
        addGroup(state, group);
    }

    if (isFiltering(state) && lv_obj_get_child_count(state->groups) == 0) {
        addLabel(state->groups, "Nothing matches", Theme::FONT_BODY, Theme::TEXT_MUTED);
    }

    lv_obj_update_layout(state->groups);
    lv_obj_scroll_to_y(state->groups, scrollY, LV_ANIM_OFF);
}

// Observers: their user data is the app-lifetime context; the list's state comes from the target's user data

static void onVersionChanged(lv_observer_t* observer, lv_subject_t* subject) {
    rebuild(getState(lv_observer_get_target_obj(observer)));
}

static void onProgressChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext* context = static_cast<UiContext*>(lv_observer_get_user_data(observer));
    UiState::Motor& motor = context->state.motor;
    lv_obj_t* label = lv_observer_get_target_obj(observer);

    if (!lv_subject_get_int(&motor.isConnected)) {
        lv_label_set_text(label, "No motor connected");
    } else if (lv_subject_get_int(&motor.isReadingParameters)) {
        lv_label_set_text_fmt(label, "Reading %" PRId32 " of %" PRId32 "…", lv_subject_get_int(&motor.parametersRead), lv_subject_get_int(&motor.parametersTotal));
    } else if (lv_subject_get_int(&motor.parametersTotal) > 0) {
        int32_t unsavedCount = lv_subject_get_int(&motor.unsavedChangeCount);

        if (unsavedCount > 0) {
            lv_label_set_text_fmt(label, "%" PRId32 " read, %" PRId32 " unsaved", lv_subject_get_int(&motor.parametersRead), unsavedCount);
        } else {
            lv_label_set_text_fmt(label, "%" PRId32 " parameters read", lv_subject_get_int(&motor.parametersRead));
        }
    } else {
        lv_label_set_text(label, "Not read yet");
    }
}

static void onConnectedChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiContext* context = static_cast<UiContext*>(lv_observer_get_user_data(observer));

    // Every drive that connects is read; the read also runs once when the list opens with one connected
    if (lv_subject_get_int(subject)) {
        context->motor.requestReadParameters();
    }
}

static void onRefreshClicked(lv_event_t* event) {
    static_cast<UiContext*>(lv_event_get_user_data(event))->motor.requestReadParameters();
}

// Filter (PAR-013)

/**
 * Takes the filter box's text as the filter: lower case, and only from MIN_FILTER_LENGTH characters. Rebuilds the
 * list when the filter changes, from the top.
 *
 * @param state List state
 */
static void applyFilter(ParameterListState* state) {
    const char* text = lv_textarea_get_text(state->filterBox);
    char filter[sizeof(state->filter)] = "";

    if (strlen(text) >= MIN_FILTER_LENGTH) {
        size_t length = 0;

        for (; text[length] != '\0' && length < sizeof(filter) - 1; length++) {
            filter[length] = static_cast<char>(tolower(static_cast<unsigned char>(text[length])));
        }

        filter[length] = '\0';
    }

    if (strcmp(filter, state->filter) == 0) {
        return;
    }

    // Starting to filter: every group with a match opens
    if (!isFiltering(state)) {
        for (bool& isOpen : state->isFilterExpanded) {
            isOpen = true;
        }
    }

    strcpy(state->filter, filter);
    rebuild(state);
    lv_obj_scroll_to_y(state->groups, 0, LV_ANIM_OFF);
}

/**
 * Shows or hides the filter box's caret. The default theme draws it as a left border on the cursor part, styled for
 * the focused state only, and the box is focused on its own; overriding that same selector hides it until typing.
 *
 * @param filterBox The text area
 * @param isShown True while typing
 */
static void showCaret(lv_obj_t* filterBox, bool isShown) {
    lv_style_selector_t focusedCursor = static_cast<lv_style_selector_t>(LV_PART_CURSOR) | LV_STATE_FOCUSED;
    lv_obj_set_style_border_opa(filterBox, isShown ? LV_OPA_COVER : LV_OPA_TRANSP, focusedCursor);
}

/**
 * Shows or hides the keyboard. While it shows, the list gets room at the bottom, so every match can be scrolled
 * above it.
 *
 * @param state List state
 * @param isOpen True to show
 */
static void showKeyboard(ParameterListState* state, bool isOpen) {
    showCaret(state->filterBox, isOpen);

    if (isOpen) {
        lv_obj_remove_flag(state->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_bottom(state->groups, KEYBOARD_HEIGHT, 0);
    } else {
        lv_obj_add_flag(state->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_bottom(state->groups, 0, 0);
        lv_obj_remove_state(state->filterBox, LV_STATE_FOCUSED);
    }
}

static void onFilterClicked(lv_event_t* event) {
    showKeyboard(static_cast<ParameterListState*>(lv_event_get_user_data(event)), true);
}

static void onFilterTimer(lv_timer_t* timer) {
    lv_timer_pause(timer);
    applyFilter(static_cast<ParameterListState*>(lv_timer_get_user_data(timer)));
}

static void onFilterChanged(lv_event_t* event) {
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));

    // A rebuild per keystroke makes typing lag: wait for a pause
    lv_timer_reset(state->filterTimer);
    lv_timer_resume(state->filterTimer);
}

static void onKeyboardDone(lv_event_t* event) {
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));

    // OK and hide apply the filter at once
    lv_timer_pause(state->filterTimer);
    applyFilter(state);
    showKeyboard(state, false);
}

/**
 * Returns the keyboard's font: Inter for the letters, falling back to LVGL's Montserrat for the key symbols
 * (backspace, OK, hide, arrows), which Inter doesn't have.
 *
 * @return The font
 */
static const lv_font_t* getKeyboardFont() {
    static lv_font_t font;
    static bool isMade = false;

    if (!isMade) {
        font = *Theme::FONT_BODY_LARGE;
        font.fallback = &lv_font_montserrat_24;
        isMade = true;
    }

    return &font;
}

static void onClearFilterClicked(lv_event_t* event) {
    ParameterListState* state = static_cast<ParameterListState*>(lv_event_get_user_data(event));

    // Clearing the text reports a change, which applies the empty filter
    lv_textarea_set_text(state->filterBox, "");
}

/**
 * Creates the filter box and its clear button in the status row, and the hidden keyboard over the list.
 *
 * @param state List state
 * @param statusRow Row for the box and its clear button
 * @param container The list's container, for the keyboard
 */
static void createFilter(ParameterListState* state, lv_obj_t* statusRow, lv_obj_t* container) {
    lv_obj_t* filterBox = lv_textarea_create(statusRow);
    lv_textarea_set_one_line(filterBox, true);
    lv_textarea_set_placeholder_text(filterBox, "Filter: name, text or index");
    lv_textarea_set_max_length(filterBox, sizeof(state->filter) - 1);
    lv_obj_set_width(filterBox, FILTER_BOX_WIDTH);
    lv_obj_set_style_text_font(filterBox, Theme::FONT_BODY, 0);
    lv_obj_add_event_cb(filterBox, onFilterClicked, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(filterBox, onFilterChanged, LV_EVENT_VALUE_CHANGED, state);
    state->filterBox = filterBox;

    // The caret only while typing. A new text area joins the default input group, which focuses it at once and shows
    // the caret; out of the group it is focused only by a tap
    if (lv_obj_get_group(filterBox) != nullptr) {
        lv_group_remove_obj(filterBox);
    }

    lv_obj_remove_state(filterBox, LV_STATE_FOCUSED);
    showCaret(filterBox, false);

    state->filterTimer = lv_timer_create(onFilterTimer, FILTER_DELAY_MS, state);
    lv_timer_pause(state->filterTimer);

    ButtonOptions clearOptions;
    clearOptions.icon = Icons::CLOSE;
    clearOptions.variant = ButtonVariant::GHOST;
    clearOptions.isCompact = true;
    clearOptions.onClick = onClearFilterClicked;
    clearOptions.userData = state;
    createButton(statusRow, clearOptions);

    // The keyboard floats over the bottom of the list; OK and its hide key close it, the filter stays
    lv_obj_t* keyboard = lv_keyboard_create(container);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(keyboard, lv_pct(100), KEYBOARD_HEIGHT);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, filterBox);
    lv_obj_set_style_text_font(keyboard, getKeyboardFont(), LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, onKeyboardDone, LV_EVENT_READY, state);
    lv_obj_add_event_cb(keyboard, onKeyboardDone, LV_EVENT_CANCEL, state);
    state->keyboard = keyboard;
}

static void onListDeleted(lv_event_t* event) {
    ParameterListState* state = getState(static_cast<lv_obj_t*>(lv_event_get_current_target(event)));

    if (state->filterTimer != nullptr) {
        lv_timer_delete(state->filterTimer);
    }

    delete state;
}

lv_obj_t* createParameterList(lv_obj_t* parent, UiContext& context) {
    UiState::Motor& motor = context.state.motor;

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, Theme::SPACE_S, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    auto* state = new ParameterListState();
    state->context = &context;

    // Status line: the filter, read progress and READ AGAIN
    lv_obj_t* statusRow = createBox(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(statusRow, Theme::SPACE_S, 0);
    createFilter(state, statusRow, container);

    lv_obj_t* progress = addLabel(statusRow, "", Theme::FONT_BODY, Theme::TEXT_MUTED);
    lv_obj_set_flex_grow(progress, 1);
    lv_obj_set_style_text_align(progress, LV_TEXT_ALIGN_RIGHT, 0);
    Bindings::observeAll(progress, onProgressChanged, &context, {&motor.isConnected, &motor.isReadingParameters, &motor.parametersRead, &motor.parametersTotal, &motor.unsavedChangeCount});

    ButtonOptions refreshOptions;
    refreshOptions.text = "READ AGAIN";
    refreshOptions.icon = Icons::REFRESH;
    refreshOptions.variant = ButtonVariant::SECONDARY_OUTLINE;
    refreshOptions.isCompact = true;
    refreshOptions.onClick = onRefreshClicked;
    refreshOptions.userData = &context;
    createButton(statusRow, refreshOptions);

    // The groups: fill the rest and scroll (the list stays clickable, or presses go past it and nothing scrolls)
    lv_obj_t* groups = lv_obj_create(container);
    lv_obj_remove_style_all(groups);
    lv_obj_set_width(groups, lv_pct(100));
    lv_obj_set_flex_grow(groups, 1);
    lv_obj_set_flex_flow(groups, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(groups, LV_DIR_VER);
    lv_obj_add_style(groups, &Theme::scrollbarStyle, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(groups, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(groups, Theme::SCROLLBAR_WIDTH + Theme::SPACE_S, 0);
    lv_obj_set_user_data(groups, state);
    lv_obj_add_event_cb(groups, onListDeleted, LV_EVENT_DELETE, nullptr);
    state->groups = groups;

    // Rebuilt after every read and every connect (which clears the values); reading starts with a connected drive
    lv_subject_add_observer_obj(&motor.parametersVersion, onVersionChanged, groups, &context);
    lv_subject_add_observer_obj(&motor.isConnected, onConnectedChanged, groups, &context);

    // Children draw in order: the keyboard over the list
    lv_obj_move_foreground(state->keyboard);

    return container;
}
