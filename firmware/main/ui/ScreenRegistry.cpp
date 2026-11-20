#include "ScreenRegistry.hpp"

#include "esp_log.h"
#include "ui/fonts/Icons.hpp"
#include "ui/screens/CalibrateScreen.hpp"
#include "ui/screens/FaultsScreen.hpp"
#include "ui/screens/HomeScreen.hpp"
#include "ui/screens/LatencyScreen.hpp"
#include "ui/screens/OverviewScreen.hpp"
#include "ui/screens/ParametersScreen.hpp"
#include "ui/screens/PositionScreen.hpp"
#include "ui/screens/SpecimenScreen.hpp"
#include "ui/screens/TorqueScreen.hpp"
#include "ui/screens/TuningScreen.hpp"
#include "ui/screens/VelocityScreen.hpp"

/**
 * Factory for a screen class constructed from the app context.
 *
 * @param context Services and navigation
 * @return The new screen
 */
template <typename TScreen>
static std::unique_ptr<Screen> create(UiContext& context) {
    return std::make_unique<TScreen>(context);
}

static const char* TAG = "ScreenRegistry";

// Home card order is table order: three rows of three (D-033), the drive's state first, then motion, then tuning and tools. Rows without a factory are screens that aren't built yet; "motor" cards are
// disabled while no drive is connected. On "bare" screens the top bar drops the search, CAN icon and STOP while no drive answers.
// clang-format off
static const ScreenDefinition SCREENS[] = {
    // id                        name             title                 icon                card status               factory                         home   motion  motor  bare
    {ScreenId::HOME,             "home",          "Sparky",             Icons::HOME,        "",                       &create<HomeScreen>,            false, false,  false, false},
    {ScreenId::OVERVIEW,         "overview",      "Overview",           Icons::OVERVIEW,    "Drive state, search",    &create<OverviewScreen>,        true,  false,  false, false},
    {ScreenId::FAULTS,           "faults",        "Faults & reset",     Icons::FAULT,       "Alarms and history",     &create<FaultsScreen>,          true,  false,  true,  false},
    {ScreenId::PARAMETERS,       "parameters",    "Parameters",         Icons::SETTINGS,    "Setup, every parameter", &create<ParametersScreen>,      true,  false,  true,  false},
    {ScreenId::VELOCITY,         "velocity",      "Velocity",           Icons::VELOCITY,    "Run at a set speed",     &create<VelocityScreen>,        true,  true,   true,  false},
    {ScreenId::POSITION,         "position",      "Position",           Icons::POSITION,    "Move to a target",       &create<PositionScreen>,        true,  true,   true,  false},
    {ScreenId::TORQUE,           "torque",        "Torque",             Icons::TORQUE,      "Push with a set torque", &create<TorqueScreen>,          true,  true,   true,  false},
    {ScreenId::TUNING,           "tuning",        "Tuning",             Icons::PARAMETERS,  "Gains and step test",    &create<TuningScreen>,          true,  true,   true,  false},
    {ScreenId::CALIBRATE,        "calibrate",     "Calibrate",          Icons::TOOLS,       "Identify angle, R, L",   &create<CalibrateScreen>,       true,  true,   true,  false},
    {ScreenId::LATENCY,          "latency",       "Latency",            Icons::LATENCY,     "Camera to screen delay", &create<LatencyScreen>,         true,  false,  false, true},
    {ScreenId::SPECIMEN,         "specimen",      "Font specimen",      Icons::INFO,        "",                       &create<SpecimenScreen>,        false, false,  false, true},
};
// clang-format on

namespace ScreenRegistry {
std::span<const ScreenDefinition> getAll() {
    return SCREENS;
}

const ScreenDefinition& get(ScreenId screenId) {
    for (const ScreenDefinition& definition : SCREENS) {
        if (definition.id == screenId) {
            return definition;
        }
    }

    // Every ScreenId has a row; fall back to home rather than crash if one is forgotten
    ESP_LOGE(TAG, "No registry row for screen %d", static_cast<int>(screenId));

    return SCREENS[0];
}
} // namespace ScreenRegistry
