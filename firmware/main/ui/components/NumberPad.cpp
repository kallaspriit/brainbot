#include "NumberPad.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui/Bindings.hpp"
#include "ui/Theme.hpp"
#include "ui/components/Button.hpp"
#include "ui/fonts/Icons.hpp"

// Layout
static constexpr int32_t PAD_WIDTH = 420;
static constexpr int32_t KEY_HEIGHT = 64;
static constexpr lv_opa_t BACKDROP_OPACITY = LV_OPA_60;

// Characters typed, not counting the sign: digits and a decimal point; enough for any int32 target
static constexpr size_t MAX_CHARACTERS = 10;

/**
 * Keys other than digits, stored in the key buttons' user data next to the digits 0-9.
 */
enum PadKey : intptr_t {
    KEY_SIGN = 10,
    KEY_BACKSPACE = 11,
    KEY_POINT = 12,
};

/**
 * What an open pad needs, owned by its backdrop and freed with it.
 */
struct NumberPad {
    NumberPadOptions options;       // Copy of the open options
    lv_obj_t* backdrop;             // Full-screen dimmer, the pad's root
    lv_obj_t* rangeLabel;           // Allowed range, red after an out-of-range OK
    lv_obj_t* valueLabel;           // Typed number
    char typed[MAX_CHARACTERS + 1]; // Typed digits and decimal point, no sign
    bool isNegative;                // Sign of the typed number
    bool isEdited;                  // A key was pressed; until then the current value is shown
};

/**
 * Shows the typed number, or the current value (muted) before the first key.
 *
 * @param pad The pad
 */
static void refreshValue(NumberPad* pad) {
    if (!pad->isEdited) {
        char text[24];
        Bindings::formatFixedPoint(text, sizeof(text), pad->options.value, pad->options.decimals, "");
        lv_label_set_text(pad->valueLabel, text);
        lv_obj_set_style_text_color(pad->valueLabel, Theme::color(Theme::TEXT_MUTED), 0);

        return;
    }

    const char* shown = pad->typed[0] != '\0' ? pad->typed : "0";
    lv_label_set_text_fmt(pad->valueLabel, "%s%s", pad->isNegative ? "-" : "", shown);
    lv_obj_set_style_text_color(pad->valueLabel, Theme::color(Theme::TEXT), 0);
}

/**
 * Returns the number on the pad in units of 10^-decimals: the typed one, or the current value before the first key.
 *
 * @param pad The pad
 * @return The value
 */
static int64_t getValue(const NumberPad* pad) {
    if (!pad->isEdited) {
        return pad->options.value;
    }

    // Whole part, then the fraction digits padded to the pad's decimals
    int64_t value = 0;
    int fractionDigits = -1;

    for (const char* character = pad->typed; *character != '\0'; character++) {
        if (*character == '.') {
            fractionDigits = 0;

            continue;
        }

        value = value * 10 + (*character - '0');

        if (fractionDigits >= 0) {
            fractionDigits++;
        }
    }

    for (int digit = fractionDigits < 0 ? 0 : fractionDigits; digit < pad->options.decimals; digit++) {
        value *= 10;
    }

    return pad->isNegative ? -value : value;
}

/**
 * Closes the pad. Deferred, since it is called from the pad's own buttons.
 *
 * @param pad The pad
 */
static void closePad(NumberPad* pad) {
    lv_obj_delete_async(pad->backdrop);
}

/**
 * Handles a digit, the decimal point, ± or backspace.
 *
 * @param event The click event; its user data is the pad, the button's user data the key
 */
static void onKeyClicked(lv_event_t* event) {
    NumberPad* pad = static_cast<NumberPad*>(lv_event_get_user_data(event));
    intptr_t key = reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));

    // The first key starts a new number rather than editing the current value
    if (!pad->isEdited) {
        pad->isEdited = true;
        pad->typed[0] = '\0';
        pad->isNegative = false;
    }

    size_t length = std::strlen(pad->typed);
    const char* point = std::strchr(pad->typed, '.');
    size_t fractionDigits = point != nullptr ? std::strlen(point + 1) : 0;

    if (key == KEY_SIGN) {
        pad->isNegative = !pad->isNegative;
    } else if (key == KEY_BACKSPACE) {
        if (length > 0) {
            pad->typed[length - 1] = '\0';
        }
    } else if (key == KEY_POINT) {
        // One point, and a leading zero so ".5" reads as "0.5"
        if (point == nullptr && length + 2 <= MAX_CHARACTERS) {
            std::strcat(pad->typed, length == 0 ? "0." : ".");
        }
    } else if (length < MAX_CHARACTERS && (point == nullptr || fractionDigits < pad->options.decimals)) {
        // No leading zeros in the whole part, and no more fraction digits than the pad's decimals
        bool isLeadingZero = length == 1 && pad->typed[0] == '0';

        if (isLeadingZero) {
            pad->typed[0] = static_cast<char>('0' + key);
        } else {
            pad->typed[length] = static_cast<char>('0' + key);
            pad->typed[length + 1] = '\0';
        }
    }

    lv_obj_set_style_text_color(pad->rangeLabel, Theme::color(Theme::TEXT_MUTED), 0);
    refreshValue(pad);
}

/**
 * Applies the number if it is in range, otherwise flags the range.
 *
 * @param event The click event; its user data is the pad
 */
static void onOkClicked(lv_event_t* event) {
    NumberPad* pad = static_cast<NumberPad*>(lv_event_get_user_data(event));
    int64_t value = getValue(pad);

    if (value < pad->options.min || value > pad->options.max) {
        lv_obj_set_style_text_color(pad->rangeLabel, Theme::color(Theme::DANGER), 0);

        return;
    }

    if (pad->options.onConfirm != nullptr) {
        pad->options.onConfirm(static_cast<int32_t>(value), pad->options.userData);
    }

    closePad(pad);
}

/**
 * Closes the pad without a change.
 *
 * @param event The click event; its user data is the pad
 */
static void onCancelClicked(lv_event_t* event) {
    closePad(static_cast<NumberPad*>(lv_event_get_user_data(event)));
}

/**
 * Closes the pad when the dimmed area around it is tapped.
 *
 * @param event The click event on the backdrop; its user data is the pad
 */
static void onBackdropClicked(lv_event_t* event) {
    // Only taps on the backdrop itself; the pad's own clicks don't bubble here
    if (lv_event_get_target_obj(event) == lv_event_get_current_target_obj(event)) {
        closePad(static_cast<NumberPad*>(lv_event_get_user_data(event)));
    }
}

/**
 * Frees the pad state when the backdrop is deleted.
 *
 * @param event The delete event; its user data is the pad
 */
static void onDeleted(lv_event_t* event) {
    delete static_cast<NumberPad*>(lv_event_get_user_data(event));
}

/**
 * Adds one key to the keypad grid.
 *
 * @param grid Keypad
 * @param pad The pad
 * @param text Key text, or empty for an icon key
 * @param icon Icons:: constant, or nullptr
 * @param key Key code: a digit or a PadKey
 * @param column Grid column
 * @param row Grid row
 */
static void addKey(lv_obj_t* grid, NumberPad* pad, const char* text, const char* icon, intptr_t key, int32_t column, int32_t row) {
    ButtonOptions options;
    options.text = text;
    options.icon = icon;
    options.height = KEY_HEIGHT;
    options.onClick = onKeyClicked;
    options.userData = pad;
    lv_obj_t* button = createButton(grid, options);
    lv_obj_set_style_pad_hor(button, 0, 0);
    lv_obj_set_user_data(button, reinterpret_cast<void*>(key));
    lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, column, 1, LV_GRID_ALIGN_STRETCH, row, 1);
}

void openNumberPad(lv_obj_t* overlayLayer, const NumberPadOptions& options) {
    NumberPad* pad = new NumberPad {};
    pad->options = options;

    // Backdrop: dims the screen and catches every touch, so nothing behind the pad can be pressed
    lv_obj_t* backdrop = lv_obj_create(overlayLayer);
    pad->backdrop = backdrop;
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, BACKDROP_OPACITY, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(backdrop, onBackdropClicked, LV_EVENT_CLICKED, pad);
    lv_obj_add_event_cb(backdrop, onDeleted, LV_EVENT_DELETE, pad);

    // The pad: title, range, value, keys, actions
    lv_obj_t* panel = lv_obj_create(backdrop);
    lv_obj_remove_style_all(panel);
    lv_obj_add_style(panel, &Theme::surfaceStyle, 0);
    lv_obj_set_style_bg_color(panel, Theme::color(Theme::SURFACE_RAISED), 0);
    lv_obj_set_size(panel, PAD_WIDTH, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, Theme::SPACE_S, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(panel);
    lv_label_set_text(titleLabel, options.title);
    lv_obj_set_width(titleLabel, lv_pct(100));
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(titleLabel, &Theme::textStyle, 0);
    lv_obj_set_style_text_font(titleLabel, Theme::FONT_TITLE, 0);

    char minText[24];
    char maxText[24];
    Bindings::formatFixedPoint(minText, sizeof(minText), options.min, options.decimals, "");
    Bindings::formatFixedPoint(maxText, sizeof(maxText), options.max, options.decimals, "");
    pad->rangeLabel = lv_label_create(panel);
    lv_label_set_text_fmt(pad->rangeLabel, "%s to %s %s", minText, maxText, options.unit);
    lv_obj_set_style_text_font(pad->rangeLabel, Theme::FONT_BODY_SMALL, 0);
    lv_obj_set_style_text_color(pad->rangeLabel, Theme::color(Theme::TEXT_MUTED), 0);

    pad->valueLabel = lv_label_create(panel);
    lv_obj_set_width(pad->valueLabel, lv_pct(100));
    lv_obj_set_style_text_align(pad->valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(pad->valueLabel, Theme::FONT_NUMBER_LARGE, 0);
    refreshValue(pad);

    // Keys: 7 8 9 ⌫ / 4 5 6 ± / 1 2 3 . / 0 across the bottom
    static const int32_t COLUMNS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t ROWS[] = {KEY_HEIGHT, KEY_HEIGHT, KEY_HEIGHT, KEY_HEIGHT, LV_GRID_TEMPLATE_LAST};
    lv_obj_t* grid = lv_obj_create(panel);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, COLUMNS, ROWS);
    lv_obj_set_style_pad_row(grid, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_column(grid, Theme::SPACE_S, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE);

    for (int32_t digit = 1; digit <= 9; digit++) {
        char text[2] = {static_cast<char>('0' + digit), '\0'};
        addKey(grid, pad, text, nullptr, digit, (digit - 1) % 3, 2 - (digit - 1) / 3);
    }

    addKey(grid, pad, "", Icons::BACK, KEY_BACKSPACE, 3, 0);

    if (options.min < 0) {
        addKey(grid, pad, "±", nullptr, KEY_SIGN, 3, 1);
    }

    if (options.decimals > 0) {
        addKey(grid, pad, ".", nullptr, KEY_POINT, 3, 2);
    }

    // Zero spans the bottom row
    ButtonOptions zeroOptions;
    zeroOptions.text = "0";
    zeroOptions.height = KEY_HEIGHT;
    zeroOptions.onClick = onKeyClicked;
    zeroOptions.userData = pad;
    lv_obj_t* zeroButton = createButton(grid, zeroOptions);
    lv_obj_set_user_data(zeroButton, reinterpret_cast<void*>(static_cast<intptr_t>(0)));
    lv_obj_set_grid_cell(zeroButton, LV_GRID_ALIGN_STRETCH, 0, 4, LV_GRID_ALIGN_STRETCH, 3, 1);

    // Cancel and OK
    lv_obj_t* actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::SPACE_S, 0);
    lv_obj_set_style_pad_top(actions, Theme::SPACE_S, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_CLICKABLE);

    ButtonOptions cancelOptions;
    cancelOptions.text = "Cancel";
    cancelOptions.onClick = onCancelClicked;
    cancelOptions.userData = pad;
    lv_obj_set_flex_grow(createButton(actions, cancelOptions), 1);

    ButtonOptions okOptions;
    okOptions.text = "OK";
    okOptions.icon = Icons::CHECK;
    okOptions.variant = ButtonVariant::PRIMARY;
    okOptions.onClick = onOkClicked;
    okOptions.userData = pad;
    lv_obj_set_flex_grow(createButton(actions, okOptions), 1);
}
