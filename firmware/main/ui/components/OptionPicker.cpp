#include "OptionPicker.hpp"

#include "ui/Theme.hpp"
#include "ui/components/Button.hpp"
#include "ui/components/Dialog.hpp"
#include "ui/fonts/Icons.hpp"

static constexpr int32_t PICKER_WIDTH = 640;

/**
 * What an option button needs, owned by the button and freed with it.
 */
struct PickAction {
    int32_t value;               // The option's value
    OptionPickerCallback onPick; // From the options
    void* userData;              // From the options
};

static void onOptionClicked(lv_event_t* event) {
    PickAction* action = static_cast<PickAction*>(lv_event_get_user_data(event));

    // Copy first: closing deletes the button and its action
    int32_t value = action->value;
    OptionPickerCallback onPick = action->onPick;
    void* userData = action->userData;
    closeDialog(lv_event_get_target_obj(event));

    if (onPick != nullptr) {
        onPick(value, userData);
    }
}

static void onOptionDeleted(lv_event_t* event) {
    delete static_cast<PickAction*>(lv_event_get_user_data(event));
}

void openOptionPicker(lv_obj_t* overlayLayer, const OptionPickerOptions& options) {
    DialogOptions dialogOptions;
    dialogOptions.title = options.title;
    dialogOptions.subtitle = options.subtitle;
    dialogOptions.width = PICKER_WIDTH;
    lv_obj_t* body = openDialog(overlayLayer, dialogOptions);

    // One button per option; the current one in the accent colour with a check
    for (size_t index = 0; index < options.optionCount; index++) {
        const PickerOption& option = options.options[index];
        bool isCurrent = option.value == options.current;

        ButtonOptions buttonOptions;
        buttonOptions.text = option.label;
        buttonOptions.icon = isCurrent ? Icons::CHECK : nullptr;
        buttonOptions.variant = isCurrent ? ButtonVariant::PRIMARY : ButtonVariant::SECONDARY;
        buttonOptions.onClick = onOptionClicked;
        buttonOptions.userData = new PickAction {option.value, options.onPick, options.userData};
        lv_obj_t* button = createButton(body, buttonOptions);
        lv_obj_set_width(button, lv_pct(100));
        lv_obj_add_event_cb(button, onOptionDeleted, LV_EVENT_DELETE, buttonOptions.userData);
    }
}
