#pragma once

#include "ui/Screen.hpp"

/**
 * Moves the motor in profile position mode (UC-5, Position mode plan): position in turns from the console zero on the
 * left, the pending target, GO and HALT on the right. Editing the target never moves the motor; GO does (D3). STOP is
 * the top bar's.
 *
 * The second button is HALT while moving and RELEASE while holding (D5); the message shows where a move settled.
 * A toggle in the target header switches between an absolute target and a relative move (D4). Zero here and the chart
 * come in POS-005 and POS-007.
 */
class PositionScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

    /**
     * Ends a running velocity test, since this screen's mode takes control (D-022).
     */
    void onEnter() override;

  private:
    /**
     * Builds the readout: position in turns, raw counts and the last message.
     *
     * @param parent Content area
     */
    void createReadout(lv_obj_t* parent);

    /**
     * Builds the target control, GO and HALT.
     *
     * @param parent Content area
     */
    void createControls(lv_obj_t* parent);

    /**
     * Sends a new pending target to the motor service.
     *
     * @param targetMilliturns New target, 0.001 turn from the zero
     * @param userData The MotorService
     */
    /**
     * Adds the Absolute / Relative toggle at the start of a target control's header.
     *
     * @param header Header row of the control
     */
    void addModeToggle(lv_obj_t* header);

    static void onTargetChanged(int32_t targetMilliturns, void* userData);

    /**
     * Sends a new pending relative move to the motor service.
     *
     * @param distanceMilliturns New distance, 0.001 turn
     * @param userData The MotorService
     */
    static void onMoveByChanged(int32_t distanceMilliturns, void* userData);

    /**
     * Switches between absolute and relative moves.
     *
     * @param index 0 absolute, 1 relative
     * @param userData The MotorService
     */
    static void onModeChanged(int32_t index, void* userData);

    /**
     * Sends a new move speed to the motor service.
     *
     * @param speedRpm New speed
     * @param userData The MotorService
     */
    static void onMoveSpeedChanged(int32_t speedRpm, void* userData);

    /**
     * Sends a new acceleration to the motor service; shared with the Velocity screen.
     *
     * @param accelerationRpmPerSecond New acceleration
     * @param userData The MotorService
     */
    static void onAccelerationChanged(int32_t accelerationRpmPerSecond, void* userData);

    /**
     * Requests the move.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onGoClicked(lv_event_t* event);

    /**
     * Shows GO as an outline while it wouldn't move the shaft.
     *
     * @param observer The observer; its target is the GO button
     * @param subject UiState's isGoWithoutEffect
     */
    static void onGoLookChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Requests a halt.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onHaltClicked(lv_event_t* event);

    /**
     * Requests a release: power stage off, shaft free.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onReleaseClicked(lv_event_t* event);

    /**
     * Requests the console zero at the current position.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onZeroClicked(lv_event_t* event);
};
