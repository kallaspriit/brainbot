#pragma once

#include "lvgl.h"
#include "ui/UiContext.hpp"

/**
 * The bar across the top of every screen, on LVGL's top layer so it stays while screens change.
 *
 * Left: back button and screen title. Right: the drive (name and serial, CiA 402 state or fault badge, warning badge, bus
 * voltage), the CAN controller state, and STOP at the right edge (UX rule: STOP never moves). Without a drive, the drive
 * part is replaced by "Searching for a motor" or the CAN failure. Everything is bound to UiState.
 */
class TopBar {
  public:
    /**
     * Creates the bar on the display's top layer.
     *
     * @param context State to show, the motor for STOP, navigation for back
     */
    explicit TopBar(UiContext& context);

    /**
     * Sets the title shown next to the back button.
     *
     * @param title Screen title
     */
    void setTitle(const char* title);

    /**
     * Switches between home, which shows "SPARKY", and the other screens, which show back and
     * the title.
     *
     * @param isHome True on the home screen
     */
    void setHome(bool isHome);

    /**
     * Lets a screen that needs no motor drop the search text, the CAN icon and STOP while no drive answers (D-036).
     * They come back as soon as a drive is connected, so a running motor always has its STOP.
     *
     * @param isOptional True on such a screen
     */
    void setMotorPartOptional(bool isOptional);

  private:
    UiContext& context;
    lv_obj_t* bar = nullptr;
    lv_obj_t* backButton = nullptr;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* brandGroup = nullptr;      // "SPARKY", on home instead of back and title
    lv_obj_t* driveGroup = nullptr;      // Identity, badges and voltage; hidden without a drive
    lv_obj_t* nameLabel = nullptr;       // Drive name (1008)
    lv_obj_t* serialLabel = nullptr;     // Serial number (1018:04)
    lv_obj_t* stateBadge = nullptr;      // CiA 402 state
    lv_obj_t* errorBadge = nullptr;      // Error code (603F), hidden when 0
    lv_obj_t* voltageLabel = nullptr;    // Bus voltage
    lv_obj_t* connectionGroup = nullptr; // Icon and label shown without a drive; tapping goes home
    lv_obj_t* connectionIcon = nullptr;  // Info icon
    lv_obj_t* connectionLabel = nullptr; // "Searching for a motor", the search step or the CAN failure
    lv_obj_t* canIcon = nullptr;         // CAN controller state
    lv_obj_t* stopButton = nullptr;
    bool isMotorPartOptional = false; // See setMotorPartOptional()

    /**
     * Builds the drive part: identity, badges, voltage, and the label shown in their place without a drive.
     */
    void createDriveGroup();

    /**
     * Shows the drive part, or the search or CAN failure text in its place, or neither (with the CAN icon and STOP) on
     * a screen where they are optional and no drive answers.
     */
    void updateConnection();

    /**
     * Builds the CAN icon and STOP.
     */
    void createStopArea();

    /**
     * Goes back when the back button is tapped.
     *
     * @param event The click event; its user data is the TopBar
     */
    static void onBackClicked(lv_event_t* event);

    /**
     * Opens the Faults screen from a fault or warning badge.
     *
     * @param event The click event; its user data is the TopBar
     */
    static void onBadgeClicked(lv_event_t* event);

    /**
     * Goes home, where the search and the checklist are, from the connection area.
     *
     * @param event The click event; its user data is the TopBar
     */
    static void onConnectionClicked(lv_event_t* event);

    /**
     * Hides the bus voltage while there's no reading yet.
     *
     * @param observer The observer; its target is the voltage label
     * @param subject The bus voltage
     */
    static void onVoltageReadingChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Shows the drive part, or the search or CAN failure text in its place.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onConnectionChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Shows the drive name and serial number.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onIdentityChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Updates the state badge: grey off, green enabled, amber quick stop or warning bit; in a fault, red with the
     * fault's short name.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onStateChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Shows an amber badge with the alarm's short name while the error code isn't 0 and the drive isn't faulted.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onErrorChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Colours the bus voltage amber while it is under the pack cutoff.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onCutoffChanged(lv_observer_t* observer, lv_subject_t* subject);

    /**
     * Colours the CAN icon by the controller state: green OK, amber warning or error passive, red bus-off or failed.
     *
     * @param observer The observer; its user data is the TopBar
     * @param subject The subject that changed
     */
    static void onCanChanged(lv_observer_t* observer, lv_subject_t* subject);

    // Constants
    static constexpr int32_t STOP_WIDTH = 150;       // Wide enough to hit without looking
    static constexpr int32_t BRAND_LETTER_SPACE = 4; // "SPARKY", as on the splash
};
