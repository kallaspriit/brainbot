#include "ConfirmDialog.hpp"

#include "ui/Theme.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/Dialog.hpp"
#include "ui/fonts/Icons.hpp"

// Narrower than a list dialog: a few lines of text and two buttons
static constexpr int32_t CONFIRM_WIDTH = 720;

/**
 * What the confirm button needs after the dialog is built, owned by the button and freed with it.
 */
struct ConfirmAction {
    ConfirmCallback onConfirm; // From the options
    void* userData;            // From the options
};

static void onCancelClicked(lv_event_t* event) {
    closeDialog(lv_event_get_target_obj(event));
}

static void onConfirmClicked(lv_event_t* event) {
    ConfirmAction* action = static_cast<ConfirmAction*>(lv_event_get_user_data(event));

    // Copy first: closing deletes the button and its action
    ConfirmCallback onConfirm = action->onConfirm;
    void* userData = action->userData;
    closeDialog(lv_event_get_target_obj(event));

    if (onConfirm != nullptr) {
        onConfirm(userData);
    }
}

static void onConfirmDeleted(lv_event_t* event) {
    delete static_cast<ConfirmAction*>(lv_event_get_user_data(event));
}

void openConfirmDialog(lv_obj_t* overlayLayer, const ConfirmDialogOptions& options) {
    DialogOptions dialogOptions;
    dialogOptions.title = options.title;
    dialogOptions.subtitle = options.subtitle;
    dialogOptions.width = CONFIRM_WIDTH;
    lv_obj_t* body = openDialog(overlayLayer, dialogOptions);

    // The points, one per line with a bullet
    for (size_t index = 0; index < options.pointCount; index++) {
        lv_obj_t* label = lv_label_create(body);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(label, "•  %s", options.points[index]);
        lv_obj_add_style(label, &Theme::textStyle, 0);
        lv_obj_set_style_text_font(label, Theme::FONT_BODY, 0);
    }

    // Exactly what goes to the drive
    if (options.writes[0] != '\0') {
        lv_obj_t* writesLabel = lv_label_create(body);
        lv_obj_set_width(writesLabel, lv_pct(100));
        lv_label_set_text_fmt(writesLabel, "Writes %s", options.writes);
        lv_obj_add_style(writesLabel, &Theme::mutedTextStyle, 0);
        lv_obj_set_style_text_font(writesLabel, Theme::FONT_CODE, 0);
        lv_obj_set_style_pad_top(writesLabel, Theme::SPACE_S, 0);
    }

    // CANCEL and the confirm action, below the body so they never scroll away
    lv_obj_t* actions = addDialogActions(body);

    ButtonOptions cancelOptions;
    cancelOptions.text = "CANCEL";
    cancelOptions.icon = Icons::CANCEL;
    cancelOptions.onClick = onCancelClicked;
    lv_obj_t* cancelButton = createButton(actions, cancelOptions);
    lv_obj_set_flex_grow(cancelButton, 1);

    ConfirmAction* action = new ConfirmAction {options.onConfirm, options.userData};
    ButtonOptions confirmOptions;
    confirmOptions.text = options.confirmText;
    confirmOptions.variant = ButtonVariant::DANGER;
    confirmOptions.onClick = onConfirmClicked;
    confirmOptions.userData = action;
    lv_obj_t* confirmButton = createButton(actions, confirmOptions);
    lv_obj_set_flex_grow(confirmButton, 1);
    lv_obj_add_event_cb(confirmButton, onConfirmDeleted, LV_EVENT_DELETE, action);
}
