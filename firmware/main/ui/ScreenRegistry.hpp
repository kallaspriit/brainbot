#pragma once

#include <memory>
#include <span>

#include "ui/Screen.hpp"
#include "ui/UiContext.hpp"

/**
 * Every screen the console has.
 */
enum class ScreenId {
    HOME,
    OVERVIEW,
    VELOCITY,
    POSITION,
    TORQUE,
    FAULTS,
    TUNING,
    PARAMETERS,
    CALIBRATE,
    LATENCY,
    SPECIMEN, // Debug: every font and icon
};

/**
 * Creates a screen.
 */
using ScreenFactory = std::unique_ptr<Screen> (*)(UiContext& context);

/**
 * One row of the screen registry: everything the rest of the UI needs to know about a screen.
 */
struct ScreenDefinition {
    ScreenId id;            // Identifier used in code
    const char* name;       // Lowercase name for the `open <name>` serial command
    const char* title;      // Top bar title and home card title
    const char* icon;       // Icons:: constant for the home card
    const char* cardStatus; // One line under the home card title
    ScreenFactory factory;  // nullptr while the screen isn't built: its card is disabled
    bool isOnHome;          // Shown as a card on the home screen
    bool isMotionScreen;    // Can run the motor
    bool needsMotor;        // Its home card is disabled while no drive is connected
    bool hidesMotorPart;    // The top bar drops the search, CAN icon and STOP while no drive answers (D-036)
};

/**
 * The single table of screens, read by the screen manager, the home grid and the serial commands. Adding a screen is
 * a Screen subclass and one row in ScreenRegistry.cpp.
 */
namespace ScreenRegistry {
/**
 * Returns every screen, in home card order.
 *
 * @return The screen definitions
 */
std::span<const ScreenDefinition> getAll();

/**
 * Returns one screen's definition.
 *
 * @param screenId The screen
 * @return Its definition; every ScreenId has a row
 */
const ScreenDefinition& get(ScreenId screenId);
} // namespace ScreenRegistry
