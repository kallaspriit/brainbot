#include "DebugCommands.hpp"

#include <cstdio>
#include <string>

#include "debug/CameraTest.hpp"
#include "debug/GamepadTest.hpp"
#include "debug/Screenshot.hpp"
#include "debug/TankTest.hpp"
#include "drivers/DisplayLock.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/components/Splash.hpp"

static const char* TAG = "Debug";

DebugCommands::DebugCommands(SerialConsole& console, Navigator& navigator, UiState& uiState, MotorService& motor, LatencyService& latency)
    : console(console)
    , navigator(navigator)
    , uiState(uiState)
    , motor(motor)
    , latency(latency) {
}

void DebugCommands::registerCommands() {
    console.addCommand("screenshot", "Send the display as a PNG frame (tools/screenshot.py)", [this] {
        Screenshot::captureAndSend(console.getUartPort());
    });

    console.addCommand("specimen", "Show every font and icon; tap to go back", [this] {
        withDisplayLock([this] {
            navigator.open(ScreenId::SPECIMEN);
        });
    });

    console.addCommand("splash", "Show the start-up splash and keep it until tapped", [this] {
        withDisplayLock([] {
            showSplash(true);
        });
    });

    // "open <screen>" for every registered screen, for screenshots without touching the display
    for (const ScreenDefinition& definition : ScreenRegistry::getAll()) {
        std::string command = std::string("open ") + definition.name;
        const char* description = definition.factory != nullptr ? "Open this screen" : "Not built yet";
        ScreenId screenId = definition.id;

        console.addCommand(command.c_str(), description, [this, screenId] {
            withDisplayLock([this, screenId] {
                navigator.open(screenId);
            });
        });
    }

    console.addCommand("state", "Log every UiState value", [this] {
        withDisplayLock([this] {
            uiState.log();
        });
    });

    console.addCommand("stack", "Log the least free stack the motor and LVGL tasks have had", [this] {
        ESP_LOGI(TAG, "Motor task stack: %u bytes free at the lowest", static_cast<unsigned>(motor.getStackHighWaterBytes()));

        // The LVGL task belongs to the display adapter; found by its name
        TaskHandle_t lvglTask = xTaskGetHandle("lvgl");

        if (lvglTask != nullptr) {
            ESP_LOGI(TAG, "LVGL task stack: %u bytes free at the lowest", static_cast<unsigned>(uxTaskGetStackHighWaterMark(lvglTask)));
        }
    });

    console.addCommand("perf", "Log each task's CPU share over 2 s, the heap, and the LVGL object and timer counts", [this] {
        logPerformance();
    });

    console.addCommand("gamepad", "TEST: start Bluetooth and show the gamepad view (tap to close)", [this] {
        if (gamepadTestStart()) {
            withDisplayLock([] {
                gamepadTestOpen();
            });
        }
    });

    console.addCommand("gamepad close", "TEST: close the gamepad view", [this] {
        withDisplayLock([] {
            gamepadTestClose();
        });
    });

    console.addCommand("gamepad pair", "TEST: scan 30 s for a controller in pairing mode and bond", [] {
        gamepadTestPair();
    });

    console.addCommand("gamepad disconnect", "TEST: disconnect the controller", [] {
        gamepadTestDisconnect();
    });

    console.addCommand("gamepad sc on", "TEST: pair with LE Secure Connections (fails on this board: DHKey check failed)", [] {
        gamepadTestSetSecureConnections(true);
    });

    console.addCommand("gamepad sc off", "TEST: pair with legacy pairing (default)", [] {
        gamepadTestSetSecureConnections(false);
    });

    console.addCommand("gamepad forget", "TEST: delete stored Bluetooth bonds", [] {
        gamepadTestForget();
    });

    console.addArgumentCommand("tank", "TEST: on | off | stop | nodes <left> <right> | invert left|right | max <rpm> | turn <percent> | accel <rpm/s> | status", [this](const std::string& arguments) {
        handleTankCommand(arguments);
    });

    console.addCommand("camera", "TEST: stream the MIPI-CSI camera full screen (tap to close)", [] {
        cameraTestStart();
    });

    console.addCommand("camera stop", "TEST: stop the camera stream", [] {
        cameraTestStop();
    });

    console.addCommand("back", "Go back to the previous screen", [this] {
        withDisplayLock([this] {
            navigator.back();
        });
    });

    console.addCommand("home", "Go home and forget the history", [this] {
        withDisplayLock([this] {
            navigator.home();
        });
    });

    // FLT-001, temporary: removed once M6 has its factory table
    console.addCommand("params", "Read every parameter in the table and log it (reads only)", [this] {
        motor.requestReadParameters(true);
    });

    console.addCommand("dump", "Log every object the drive answers (reads only, refused while running)", [this] {
        motor.requestDump();
    });

    // Runs the whole search, also with a drive connected at the default (D-026)
    // TRQ-001 spike: torque mode from the console, fixed targets in 0.1 % of rated (the console matches whole lines)
    static const int32_t TORQUE_TARGETS[] = {0, 50, 100, 200, 300, 500, -100, -300};

    for (int32_t target : TORQUE_TARGETS) {
        std::string command = std::string("torque ") + std::to_string(target);

        console.addCommand(command.c_str(), "Torque mode at this target, 0.1 % of rated (starts it if needed)", [this, target] {
            motor.setTargetTorquePerMille(target);

            if (!motor.getStatus().isRunning || motor.getStatus().motionMode != MotionMode::TORQUE) {
                motor.requestStartTorque();
            }
        });
    }

    console.addCommand("torque stop", "Torque to 0 at the slope, then power stage off", [this] {
        motor.requestTorqueStop();
    });

    console.addCommand("search", "Forget the drive and search past the default node and bitrate (refused while running)", [this] {
        motor.requestSearch();
    });

    // Bench recovery of a drive: the Faults and Tune screens' actions, and raw SDO access
    console.addCommand("stop", "STOP, as the top bar's button: quick stop, the run is over", [this] {
        motor.requestStop();
    });

    console.addCommand("reset", "Fault reset (controlword bit 7), as RESET FAULT on the Faults screen", [this] {
        motor.requestFaultReset();
    });

    console.addCommand("restore", "Restore defaults (1011:01 = load) without the confirmation screen", [this] {
        motor.requestRestoreDefaults();
    });

    console.addCommand("save", "Save parameters (1010:01 = save) without the confirmation screen", [this] {
        motor.requestSave();
    });

    console.addCommand("recover", "Blank-flash recovery (D-031) without the confirmation screen", [this] {
        motor.requestRecover();
    });

    console.addCommand("calibrate", "The drive's offline parameter identification (D-032) without the confirmation screen; the motor spins", [this] {
        motor.requestCalibration();
    });

    console.addCommand("fake blank", "Report a blank parameter flash until the console restarts, to exercise the recovery UI on a healthy motor", [this] {
        motor.requestFakeBlank();
    });

    console.addCommand("blank", "TEST: write all ones to every parameter the recovery covers, run time only (RECOVER or a power cycle undoes it)", [this] {
        motor.requestBlankForTest();
    });

    console.addArgumentCommand("latency",
                               "level | run | selftest | stop | off | demo | led on | led off: glass-to-glass latency (D-034); run and selftest log each sample; demo fills synthetic samples",
                               [this](const std::string& arguments) {
                                   handleLatencyCommand(arguments);
                               });

    console.addArgumentCommand("sdo", "r <index> <sub> | s <index> <sub> (string) | w <index> <sub> <bytes> <value>; index and sub in hex", [this](const std::string& arguments) {
        handleSdoCommand(arguments);
    });

    console.addArgumentCommand("nmt", "<command> in hex: 01 start, 80 pre-operational, 81 reset node (reloads the drive's flash), 82 reset communication", [this](const std::string& arguments) {
        MotorService::RawSdoRequest request;
        request.kind = MotorService::RawSdoRequest::Kind::NMT;
        request.value = static_cast<uint32_t>(strtoul(arguments.c_str(), nullptr, 16));
        motor.requestRawSdo(request);
    });
}

void DebugCommands::startTankMode() {
    // Bluetooth and the gamepad view first; the view's second line shows the tank state
    if (gamepadTestStart()) {
        withDisplayLock([] {
            gamepadTestOpen();
        });
    }

    tankTestOn();
}

void DebugCommands::handleTankCommand(const std::string& arguments) {
    unsigned first = 0;
    unsigned second = 0;
    int value = 0;

    if (arguments == "on") {
        startTankMode();
    } else if (arguments == "off") {
        tankTestOff();
    } else if (arguments == "stop") {
        tankTestStop();
    } else if (sscanf(arguments.c_str(), "nodes %u %u", &first, &second) == 2 && first >= 1 && first <= 127 && second >= 1 && second <= 127) {
        tankTestSetNodes(static_cast<uint8_t>(first), static_cast<uint8_t>(second));
    } else if (arguments == "invert left" || arguments == "invert right") {
        tankTestToggleInvert(arguments == "invert left");
    } else if (sscanf(arguments.c_str(), "max %d", &value) == 1) {
        tankTestSetMaxRpm(value);
    } else if (sscanf(arguments.c_str(), "turn %d", &value) == 1) {
        tankTestSetTurnPercent(value);
    } else if (sscanf(arguments.c_str(), "accel %d", &value) == 1) {
        tankTestSetAccelerationRpmPerSecond(value);
    } else if (arguments == "status") {
        tankTestLogStatus();
    } else {
        ESP_LOGW(TAG, "tank: unknown arguments \"%s\"", arguments.c_str());
    }
}

void DebugCommands::handleLatencyCommand(const std::string& arguments) {
    if (arguments == "level") {
        latency.setLevelLogged(true);
        latency.requestAim();
    } else if (arguments == "run") {
        latency.requestMeasurement();
    } else if (arguments == "selftest") {
        latency.requestSelfTest();
    } else if (arguments == "stop") {
        latency.setLevelLogged(false);
        latency.requestStop();
    } else if (arguments == "off") {
        latency.setLevelLogged(false);
        latency.requestIdle();
    } else if (arguments == "demo") {
        latency.requestDemo();
    } else if (arguments == "led on") {
        latency.setLedOn(true);
    } else if (arguments == "led off") {
        latency.setLedOn(false);
    } else {
        ESP_LOGW(TAG, "Usage: latency level | run | selftest | stop | off | demo | led on | led off");
    }
}

static void countObjects(lv_obj_t* object, uint32_t& count) {
    count++;

    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) {
        countObjects(lv_obj_get_child(object, static_cast<int32_t>(i)), count);
    }
}

void DebugCommands::logPerformance() {
    static constexpr UBaseType_t MAX_TASKS = 48;
    static constexpr uint32_t WINDOW_MS = 2000;

    // Two snapshots of every task's run time, a window apart
    auto* before = new TaskStatus_t[MAX_TASKS];
    auto* after = new TaskStatus_t[MAX_TASKS];
    configRUN_TIME_COUNTER_TYPE totalBefore = 0;
    configRUN_TIME_COUNTER_TYPE totalAfter = 0;
    UBaseType_t countBefore = uxTaskGetSystemState(before, MAX_TASKS, &totalBefore);
    vTaskDelay(pdMS_TO_TICKS(WINDOW_MS));
    UBaseType_t countAfter = uxTaskGetSystemState(after, MAX_TASKS, &totalAfter);

    // Both cores count, so the window holds two cores' worth of time
    uint64_t windowTotal = static_cast<uint64_t>(totalAfter - totalBefore) * portNUM_PROCESSORS;

    for (UBaseType_t i = 0; i < countAfter; i++) {
        configRUN_TIME_COUNTER_TYPE runBefore = 0;

        for (UBaseType_t j = 0; j < countBefore; j++) {
            if (before[j].xHandle == after[i].xHandle) {
                runBefore = before[j].ulRunTimeCounter;
            }
        }

        uint64_t run = after[i].ulRunTimeCounter - runBefore;
        unsigned perMille = windowTotal > 0 ? static_cast<unsigned>(run * 1000 / windowTotal) : 0;

        if (perMille > 0) {
            ESP_LOGI(TAG,
                     "CPU %-16s %3u.%u %% (core %d, priority %u)",
                     after[i].pcTaskName,
                     perMille / 10,
                     perMille % 10,
                     static_cast<int>(after[i].xCoreID),
                     static_cast<unsigned>(after[i].uxCurrentPriority));
        }
    }

    delete[] before;
    delete[] after;

    // Heap: internal and PSRAM, with the largest block for fragmentation
    ESP_LOGI(TAG,
             "Heap internal %u free (largest %u), PSRAM %u free (largest %u)",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));

    // LVGL objects on every screen and layer, and its timers: leftovers from closed screens show here
    withDisplayLock([] {
        lv_display_t* display = lv_display_get_default();
        uint32_t screenObjects = 0;
        uint32_t layerObjects = 0;
        uint32_t timerCount = 0;

        countObjects(lv_screen_active(), screenObjects);
        countObjects(lv_display_get_layer_top(display), layerObjects);
        countObjects(lv_display_get_layer_sys(display), layerObjects);

        for (lv_timer_t* timer = lv_timer_get_next(nullptr); timer != nullptr; timer = lv_timer_get_next(timer)) {
            timerCount++;
        }

        ESP_LOGI(
            TAG, "LVGL %u objects on the active screen, %u on the layers, %u timers", static_cast<unsigned>(screenObjects), static_cast<unsigned>(layerObjects), static_cast<unsigned>(timerCount));
    });
}

void DebugCommands::handleSdoCommand(const std::string& arguments) {
    // Fields: kind, index (hex), sub-index (hex), and for a write the size in bytes and the value (0x hex or decimal)
    char kind = '\0';
    char indexText[8] = "";
    char subIndexText[8] = "";
    unsigned sizeBytes = 0;
    char valueText[16] = "";
    int fieldCount = sscanf(arguments.c_str(), " %c %7s %7s %u %15s", &kind, indexText, subIndexText, &sizeBytes, valueText);

    MotorService::RawSdoRequest request;
    request.index = static_cast<uint16_t>(strtoul(indexText, nullptr, 16));
    request.subIndex = static_cast<uint8_t>(strtoul(subIndexText, nullptr, 16));

    if (fieldCount >= 3 && kind == 'r') {
        request.kind = MotorService::RawSdoRequest::Kind::READ;
    } else if (fieldCount >= 3 && kind == 's') {
        request.kind = MotorService::RawSdoRequest::Kind::READ_STRING;
    } else if (fieldCount == 5 && kind == 'w' && sizeBytes >= 1 && sizeBytes <= 4) {
        request.kind = MotorService::RawSdoRequest::Kind::WRITE;
        request.sizeBytes = static_cast<uint8_t>(sizeBytes);
        request.value = static_cast<uint32_t>(strtoll(valueText, nullptr, 0)); // Negative decimals wrap to their two's complement
    } else {
        ESP_LOGW(TAG, "Usage: sdo r <index> <sub> | sdo s <index> <sub> | sdo w <index> <sub> <bytes> <value>");

        return;
    }

    motor.requestRawSdo(request);
}

void DebugCommands::withDisplayLock(const std::function<void()>& action) {
    DisplayLock lock;

    if (lock.isLocked()) {
        action();
    }
}
