#pragma once

#include "ui/Screen.hpp"

/**
 * Runs the motor in profile torque mode (Torque mode plan): a target torque and a slope in mN·m (converted to the
 * drive's per mille of the rated current with the rated torque 6076), START and SMOOTH STOP, and a chart of the
 * target, the torque from the current and the speed.
 */
class TorqueScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

    /**
     * Ends velocity or position motion, since this screen's mode takes control (D-022).
     */
    void onEnter() override;

  private:
    /**
     * Builds the readout: current, speed, the torque chart, the speed limits and the last message.
     *
     * @param parent Content area
     */
    void createReadout(lv_obj_t* parent);

    /**
     * Builds the target torque and slope controls and START.
     *
     * @param parent Content area
     */
    void createControls(lv_obj_t* parent);

    /**
     * Sends a new target torque to the motor service, converted to per mille of the rated current.
     *
     * @param torqueDeciMilliNm New target, 0.1 mN·m
     * @param userData The UiContext
     */
    static void onTorqueChanged(int32_t torqueDeciMilliNm, void* userData);

    /**
     * Sends a new slope to the motor service, converted to per mille of the rated current per second.
     *
     * @param slopeDeciMilliNmPerSecond New slope, 0.1 mN·m per second
     * @param userData The UiContext
     */
    static void onSlopeChanged(int32_t slopeDeciMilliNmPerSecond, void* userData);

    /**
     * Requests the torque start.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onStartClicked(lv_event_t* event);

    /**
     * Requests the smooth stop.
     *
     * @param event The click event; its user data is the MotorService
     */
    static void onSmoothStopClicked(lv_event_t* event);
};
