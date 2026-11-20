#include "FaultReactionsDialog.hpp"

#include <cinttypes>

#include "protocol/AlarmCodes.hpp"
#include "protocol/AlarmWord.hpp"
#include "services/MotorService.hpp"
#include "ui/Navigator.hpp"
#include "ui/Theme.hpp"
#include "ui/components/Dialog.hpp"

// Column widths
static constexpr int32_t NAME_WIDTH = 190;
static constexpr int32_t RAW_WIDTH = 110;

/**
 * Adds one reaction row: alarm name, the reaction (and the manual's default when it differs), the raw word.
 *
 * @param body Dialog body
 * @param word The drive's alarm word from 200E
 */
static void addReactionRow(lv_obj_t* body, uint32_t word) {
    uint16_t code = AlarmWord::getCode(word);
    const AlarmCode* alarm = AlarmCodes::find(code);
    uint32_t defaultWord = 0;
    bool hasDefault = AlarmWord::findFactoryDefault(code, defaultWord);
    bool isDifferent = hasDefault && defaultWord != word;
    char reaction[64];
    AlarmWord::formatReaction(word, reaction, sizeof(reaction));

    lv_obj_t* row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::SPACE_S, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Name and reaction in the severity colour
    lv_obj_t* nameLabel = lv_label_create(row);
    lv_obj_set_width(nameLabel, NAME_WIDTH);
    lv_obj_set_style_text_color(nameLabel, Theme::color(Theme::alarmColor(AlarmWord::getResponseCode(word))), 0);

    if (alarm != nullptr) {
        lv_label_set_text(nameLabel, alarm->shortName);
    } else {
        lv_label_set_text_fmt(nameLabel, "Alarm 0x%04X", code);
    }

    // The reaction, and under it in grey what the manual's default would do, so a change is clear without the raw words
    lv_obj_t* reactionCell = lv_obj_create(row);
    lv_obj_remove_style_all(reactionCell);
    lv_obj_set_height(reactionCell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(reactionCell, 1);
    lv_obj_set_flex_flow(reactionCell, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(reactionCell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(reactionCell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* reactionLabel = lv_label_create(reactionCell);
    lv_label_set_text(reactionLabel, reaction);
    lv_obj_set_style_text_color(reactionLabel, Theme::color(Theme::alarmColor(AlarmWord::getResponseCode(word))), 0);

    if (isDifferent) {
        char defaultReaction[64];
        AlarmWord::formatReaction(defaultWord, defaultReaction, sizeof(defaultReaction));
        lv_obj_t* defaultLabel = lv_label_create(reactionCell);
        lv_label_set_text_fmt(defaultLabel, "Factory: %s", defaultReaction);
        lv_obj_add_style(defaultLabel, &Theme::mutedTextStyle, 0);
    }

    lv_obj_t* rawLabel = lv_label_create(row);
    lv_obj_set_width(rawLabel, RAW_WIDTH);
    lv_label_set_text_fmt(rawLabel, "0x%08" PRIX32, word);
    lv_obj_add_style(rawLabel, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_text_font(rawLabel, Theme::FONT_CODE, 0);
}

/**
 * Adds a muted note line.
 *
 * @param body Dialog body
 * @param text The note
 */
static void addNote(lv_obj_t* body, const char* text) {
    lv_obj_t* label = lv_label_create(body);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, &Theme::mutedTextStyle, 0);
    lv_obj_set_style_pad_top(label, Theme::SPACE_S, 0);
}

void openFaultReactionsDialog(UiContext& context) {
    MotorStatus status = context.motor.getStatus();
    const FaultRecord& record = status.faultRecord;

    DialogOptions options;
    options.title = "Fault reactions (200E)";
    options.subtitle = "What this drive does when each configurable alarm happens, and whether it clears by itself.";
    lv_obj_t* body = openDialog(context.navigator.getOverlayLayer(), options);

    // A snapshot: the reactions only change when someone writes 200E, and the dialog is closed long before that matters
    if (!status.isConnected || record.reactionCount == 0) {
        addNote(body, "Not read: no drive, or the drive has no 200E.");

        return;
    }

    size_t differentCount = 0;

    for (size_t entry = 0; entry < record.reactionCount; entry++) {
        uint32_t defaultWord = 0;

        if (AlarmWord::findFactoryDefault(AlarmWord::getCode(record.reactions[entry]), defaultWord) && defaultWord != record.reactions[entry]) {
            differentCount++;
        }

        addReactionRow(body, record.reactions[entry]);
    }

    addNote(body,
            differentCount == 0 ? "All at their factory values. Alarms not listed have a fixed reaction: fault, needs a reset."
                                : "Changed reactions show their factory value below them. Alarms not listed have a fixed reaction: fault, needs a reset.");
}
