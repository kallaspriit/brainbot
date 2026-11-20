#pragma once

#include "ui/Screen.hpp"

/**
 * Glass-to-glass latency (D-034, Latency measurement plan): the light on the sensor for aiming, the LED switch, START
 * and SELF-TEST; during a run the samples as they come in with the live median and p95, and STOP; afterwards the
 * results and a histogram. Needs no motor. Entering starts the sensor; leaving ends a run and stops it.
 */
class LatencyScreen : public Screen {
  public:
    using Screen::Screen;

    void create(lv_obj_t* content) override;

    /**
     * Starts the sensor for aiming.
     */
    void onEnter() override;

    /**
     * Ends a run, turns the LED off and stops the sensor.
     */
    void onLeave() override;

  private:
    /**
     * Builds the readout and the buttons on the right.
     *
     * @param parent The right column
     */
    void createReadout(lv_obj_t* parent);
};
