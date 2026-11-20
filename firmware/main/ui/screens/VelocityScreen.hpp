#pragma once

#include "ui/Screen.hpp"

/**
 * Runs the motor in profile velocity mode (UC-4): readout on the left, target speed, acceleration and START on the
 * right. While running, START becomes SMOOTH STOP, which ramps down with the set deceleration; the top bar STOP is the quick
 * stop. The motor keeps running when the screen is left (D-016); the controls start from the
 * service's targets.
 */
class VelocityScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

    /**
     * Ends position mode if it is moving or holding, since this screen's mode takes control (D-022).
     */
    void onEnter() override;

  private:
    /**
     * Builds the readout: velocity, position, current, the velocity chart and the last message.
     *
     * @param parent Content area
     */
    void createReadout(lv_obj_t* parent);

    /**
     * Builds the target speed and acceleration controls and START.
     *
     * @param parent Content area
     */
    void createControls(lv_obj_t* parent);

    /**
     * Sends a new target speed to the motor service.
     *
     * @param velocityRpm New target
     * @param userData The MotorService
     */
    static void onVelocityChanged(int32_t velocityRpm, void* userData);

    /**
     * Sends a new acceleration to the motor service.
     *
     * @param accelerationRpmPerSecond New acceleration
     * @param userData The MotorService
     */
    static void onAccelerationChanged(int32_t accelerationRpmPerSecond, void* userData);

    /**
     * Requests the velocity start.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onStartClicked(lv_event_t* event);

    /**
     * Requests the ramp stop.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onRampStopClicked(lv_event_t* event);
};
