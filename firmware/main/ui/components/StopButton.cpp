#include "StopButton.hpp"

#include "services/MotorService.hpp"
#include "ui/UiState.hpp"
#include "ui/components/Button.hpp"
#include "ui/fonts/Icons.hpp"

/**
 * Requests a stop.
 *
 * @param event The click event; its user data is the MotorService
 */
static void onStopClicked(lv_event_t* event) {
    static_cast<MotorService*>(lv_event_get_user_data(event))->requestStop();
}

/**
 * Switches between filled and outline as the drive's state changes.
 *
 * @param observer The observer; its target is the button, its user data the UiState
 * @param subject The subject that changed
 */
static void onStopLookChanged(lv_observer_t* observer, lv_subject_t* subject) {
    UiState::Motor& motor = static_cast<UiState*>(lv_observer_get_user_data(observer))->motor;
    DriveState state = static_cast<DriveState>(lv_subject_get_int(&motor.state));
    bool isPowered = state == DriveState::SWITCHED_ON || state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;
    bool canMove = isPowered || lv_subject_get_int(&motor.isRunning);

    setButtonVariant(lv_observer_get_target_obj(observer), canMove ? ButtonVariant::DANGER : ButtonVariant::DANGER_OUTLINE);
}

lv_obj_t* createStopButton(lv_obj_t* parent, UiContext& context, bool isCompact) {
    UiState::Motor& motor = context.state.motor;

    ButtonOptions options;
    options.text = "STOP";
    options.icon = Icons::STOP;
    options.variant = ButtonVariant::DANGER_OUTLINE;
    options.isCompact = isCompact;
    options.onClick = onStopClicked;
    options.userData = &context.motor;
    lv_obj_t* button = createButton(parent, options);

    // Bindings go with the button; UiState outlives it, so it can be the observers' user data
    lv_obj_bind_state_if_eq(button, &motor.isConnected, LV_STATE_DISABLED, 0);
    lv_subject_add_observer_obj(&motor.state, onStopLookChanged, button, &context.state);
    lv_subject_add_observer_obj(&motor.isRunning, onStopLookChanged, button, &context.state);

    return button;
}
