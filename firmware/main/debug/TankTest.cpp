// THROWAWAY (test/gamepad branch): drive a two-motor tracked robot with the Xbox controller

#include "TankTest.hpp"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "BoardConfig.hpp"
#include "debug/GamepadTest.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "protocol/Cia402.hpp"
#include "protocol/SdoClient.hpp"
#include "services/MotorService.hpp"

static const char* TAG = "Tank";

// Timing and limits
static constexpr int LOOP_PERIOD_MS = 20;                    // 50 Hz target updates
static constexpr int CHECK_EVERY_LOOPS = 25;                 // Drive state and bus voltage twice a second
static constexpr int REFRESH_EVERY_LOOPS = 25;               // Resend unchanged targets twice a second
static constexpr int64_t ARM_HOLD_US = 1000000;              // LB + RB held this long arms
static constexpr int STATE_STEP_TIMEOUT_MS = 500;            // Per CiA 402 state walk step
static constexpr int STOP_SETTLE_TIMEOUT_MS = 1500;          // Longest wait for the tracks to stop after a quick stop
static constexpr int32_t STOPPED_RPM = 5;                    // Below this a track counts as stopped
static constexpr int BUS_HANDOVER_TIMEOUT_MS = 3000;         // Longest wait for the motor service to let go of the bus
static constexpr uint32_t UNDERVOLTAGE_LIMIT_ON_PACK_V = 12; // D-005: the factory 24 V keeps a drive from enabling on the pack
static constexpr int32_t MAX_RPM_LIMIT = 3000;               // Highest full-stick speed, fast mode included
static constexpr float STICK_DEADZONE = 0.12f;               // Fraction of full stick ignored around the centre

/**
 * One track: which drive moves it and how.
 */
struct Track {
    const char* name;
    std::atomic<uint8_t> nodeId;
    std::atomic<bool> isInverted;
    int32_t encoderIncrements = 0; // 608F:01, counts per motor turn
    int32_t lastTargetRpm = 0;     // Last target written
    int32_t busVoltageDeciv = 0;   // 200B:15, 0.1 V
};

enum class TankState : uint8_t {
    OFF,
    DISARMED,
    ARMED,
};

static SdoClient* sdo = nullptr;
static MotorService* motor = nullptr;
static TaskHandle_t taskHandle = nullptr;

// Settings, from the console task
static Track leftTrack = {"left", {2}, {false}};
static Track rightTrack = {"right", {1}, {true}};
static std::atomic<int32_t> maxRpm {300}; // Full stick in normal mode; fast mode doubles it
static std::atomic<int32_t> accelerationRpmPerSecond {1000};
static std::atomic<bool> isFast {false};       // Toggled with Y
static std::atomic<int32_t> turnPercent {100}; // Turn stick's share of full speed

// Requests, from any task
static std::atomic<bool> isOnRequested {false};
static std::atomic<bool> isOffRequested {false};
static std::atomic<bool> isStopRequested {false};

// Task state
static std::atomic<TankState> tankState {TankState::OFF};
static char statusText[96] = {};

static void setStatus(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void setStatus(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(statusText, sizeof(statusText), format, arguments);
    va_end(arguments);

    gamepadTestSetSecondStatus(statusText);
    ESP_LOGI(TAG, "%s", statusText);
}

// Bus helpers, all on the tank task while the motor service has lent out the bus

static bool writeObject(Track& track, const ObjectEntry& entry, uint32_t value, const char* what) {
    sdo->setNodeId(track.nodeId);
    SdoResult result = sdo->write(entry, value);

    if (result != SdoResult::OK) {
        ESP_LOGW(TAG, "%s track (node %u): %s failed: %s", track.name, track.nodeId.load(), what, SdoClient::getResultName(result));

        return false;
    }

    return true;
}

static bool readObject(Track& track, const ObjectEntry& entry, uint32_t& value) {
    sdo->setNodeId(track.nodeId);

    return sdo->read(entry, value) == SdoResult::OK;
}

static DriveState readState(Track& track) {
    uint32_t statusword = 0;

    if (!readObject(track, Objects::STATUSWORD, statusword)) {
        return DriveState::UNKNOWN;
    }

    return decodeDriveState(static_cast<uint16_t>(statusword));
}

static int32_t rpmToCountsPerSecond(const Track& track, int32_t rpm) {
    return static_cast<int32_t>(static_cast<int64_t>(rpm) * track.encoderIncrements / 60);
}

/**
 * Writes a controlword and waits for the state it leads to.
 *
 * @param track The track's drive
 * @param controlword Controlword to write
 * @param expectedState State to wait for
 * @return True once the drive is in the expected state
 */
static bool stepTo(Track& track, uint16_t controlword, DriveState expectedState) {
    if (!writeObject(track, Objects::CONTROLWORD, controlword, "controlword")) {
        return false;
    }

    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(STATE_STEP_TIMEOUT_MS) * 1000;

    while (esp_timer_get_time() < deadlineUs) {
        if (readState(track) == expectedState) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "%s track: controlword 0x%04x didn't reach the expected state", track.name, controlword);

    return false;
}

/**
 * Configures one drive for profile velocity at speed 0 and enables it (the motor service's start sequence, cut down).
 *
 * @param track The track's drive
 * @return True once the drive is in Operation Enabled
 */
static bool armTrack(Track& track) {
    DriveState state = readState(track);

    if (state == DriveState::UNKNOWN) {
        setStatus("%s track: node %u doesn't answer", track.name, track.nodeId.load());

        return false;
    }

    // Clear a fault first, or the state walk can't leave Fault (rising edge of bit 7). Also a latched error code: a stall
    // stays in 603F without a fault and halts the next run at once (the motor service does the same, bench 2026-10-11)
    uint32_t errorCode = 0;
    readObject(track, Objects::ERROR_CODE, errorCode);

    if (state == DriveState::FAULT || errorCode != 0) {
        ESP_LOGW(TAG, "%s track: clearing %s 0x%04" PRIX32, track.name, state == DriveState::FAULT ? "fault" : "error code", errorCode);
        writeObject(track, Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "controlword");
        writeObject(track, Objects::CONTROLWORD, Controlword::FAULT_RESET, "fault reset");
    }

    // Counts per motor turn, for rpm to counts/s
    uint32_t encoderIncrements = 0;

    if (!readObject(track, Objects::ENCODER_INCREMENTS, encoderIncrements) || encoderIncrements == 0) {
        setStatus("%s track: encoder resolution (608F:01) unreadable", track.name);

        return false;
    }

    track.encoderIncrements = static_cast<int32_t>(encoderIncrements);

    uint32_t busVoltageDeciv = 0;

    if (readObject(track, Objects::BUS_VOLTAGE, busVoltageDeciv)) {
        track.busVoltageDeciv = static_cast<int32_t>(busVoltageDeciv);
    }

    // D-005: lower the bus undervoltage limit for the pack, run time only (gone at the drive's power-off)
    uint32_t undervoltageLimitV = 0;

    if (readObject(track, Objects::BUS_UNDERVOLTAGE_LIMIT_V, undervoltageLimitV) && undervoltageLimitV > UNDERVOLTAGE_LIMIT_ON_PACK_V) {
        ESP_LOGI(TAG, "%s track: undervoltage limit %" PRIu32 " V -> %" PRIu32 " V (run time only)", track.name, undervoltageLimitV, UNDERVOLTAGE_LIMIT_ON_PACK_V);
        writeObject(track, Objects::BUS_UNDERVOLTAGE_LIMIT_V, UNDERVOLTAGE_LIMIT_ON_PACK_V, "undervoltage limit");
    }

    // Profile velocity at 0 with the set ramps, then the CiA 402 state walk
    uint32_t accelerationCounts = static_cast<uint32_t>(rpmToCountsPerSecond(track, accelerationRpmPerSecond));
    bool isConfigured = writeObject(track, Objects::CONTROL_MODE, ControlMode::CIA402, "control mode") && writeObject(track, Objects::PROFILE_ACCELERATION, accelerationCounts, "acceleration") &&
                        writeObject(track, Objects::PROFILE_DECELERATION, accelerationCounts, "deceleration") &&
                        writeObject(track, Objects::MODE_OF_OPERATION, OperationMode::PROFILE_VELOCITY, "mode of operation") && writeObject(track, Objects::TARGET_VELOCITY, 0, "target velocity");

    if (!isConfigured) {
        setStatus("%s track: configuring failed", track.name);

        return false;
    }

    bool isEnabled = stepTo(track, Controlword::DISABLE_VOLTAGE, DriveState::SWITCH_ON_DISABLED) && stepTo(track, Controlword::SHUTDOWN, DriveState::READY_TO_SWITCH_ON) &&
                     stepTo(track, Controlword::SWITCH_ON, DriveState::SWITCHED_ON) && stepTo(track, Controlword::ENABLE_OPERATION, DriveState::OPERATION_ENABLED);

    if (!isEnabled) {
        uint32_t errorCode = 0;
        readObject(track, Objects::ERROR_CODE, errorCode);
        setStatus("%s track: enable failed (error 0x%04" PRIX32 ")", track.name, errorCode);

        return false;
    }

    track.lastTargetRpm = 0;

    return true;
}

/**
 * Quick-stops both drives, waits for the tracks to stop (or a timeout), then switches both power stages off.
 *
 * @param reason Shown on the gamepad view and logged
 */
static void disarm(const char* reason) {
    writeObject(leftTrack, Objects::CONTROLWORD, Controlword::QUICK_STOP, "quick stop");
    writeObject(rightTrack, Objects::CONTROLWORD, Controlword::QUICK_STOP, "quick stop");

    // Let the quick-stop ramp finish before the power stages go off, so the tracks brake rather than coast
    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(STOP_SETTLE_TIMEOUT_MS) * 1000;

    while (esp_timer_get_time() < deadlineUs) {
        uint32_t leftRpm = 0;
        uint32_t rightRpm = 0;
        bool isLeftRead = readObject(leftTrack, Objects::VELOCITY_ACTUAL, leftRpm);
        bool isRightRead = readObject(rightTrack, Objects::VELOCITY_ACTUAL, rightRpm);
        bool isLeftStopped = !isLeftRead || std::abs(static_cast<int32_t>(leftRpm)) < STOPPED_RPM;
        bool isRightStopped = !isRightRead || std::abs(static_cast<int32_t>(rightRpm)) < STOPPED_RPM;

        if (isLeftStopped && isRightStopped) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    writeObject(leftTrack, Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "power stage off");
    writeObject(rightTrack, Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "power stage off");
    leftTrack.lastTargetRpm = 0;
    rightTrack.lastTargetRpm = 0;
    tankState = TankState::DISARMED;
    setStatus("Disarmed (%s): hold LB + RB to arm", reason);
}

// Stick mixing

static float stickToUnit(uint16_t raw) {
    // 0..65535 around 0x8000 to -1..1, with the deadzone cut out and the rest stretched back to full range
    float value = (static_cast<float>(raw) - 32768.0f) / 32768.0f;
    float magnitude = std::fabs(value);

    if (magnitude < STICK_DEADZONE) {
        return 0.0f;
    }

    float scaled = std::min(1.0f, (magnitude - STICK_DEADZONE) / (1.0f - STICK_DEADZONE));

    return value < 0.0f ? -scaled : scaled;
}

/**
 * Mixes the sticks into track speeds: left stick Y is throttle, right stick X is turn.
 *
 * @param input Controller input
 * @param leftRpm Left track speed in rpm, before inversion
 * @param rightRpm Right track speed in rpm, before inversion
 */
static void mixSticks(const GamepadInput& input, int32_t& leftRpm, int32_t& rightRpm) {
    float throttle = -stickToUnit(input.leftY);                                        // Stick up is the low end
    float turn = stickToUnit(input.rightX) * static_cast<float>(turnPercent) / 100.0f; // Right turns right: the left track runs faster
    float left = throttle + turn;
    float right = throttle - turn;

    // Past full speed on either side: scale both down, keeping the turn ratio
    float largest = std::max(1.0f, std::max(std::fabs(left), std::fabs(right)));
    int32_t limitRpm = isFast ? std::min<int32_t>(maxRpm * 2, MAX_RPM_LIMIT) : maxRpm.load();
    leftRpm = static_cast<int32_t>(std::lround(left / largest * static_cast<float>(limitRpm)));
    rightRpm = static_cast<int32_t>(std::lround(right / largest * static_cast<float>(limitRpm)));
}

static bool areSticksCentred(const GamepadInput& input) {
    return stickToUnit(input.leftY) == 0.0f && stickToUnit(input.rightX) == 0.0f;
}

static void writeTarget(Track& track, int32_t rpm, bool shouldForce) {
    int32_t signedRpm = track.isInverted ? -rpm : rpm;

    if (signedRpm == track.lastTargetRpm && !shouldForce) {
        return;
    }

    if (writeObject(track, Objects::TARGET_VELOCITY, static_cast<uint32_t>(rpmToCountsPerSecond(track, signedRpm)), "target velocity")) {
        track.lastTargetRpm = signedRpm;
    }
}

/**
 * Checks both drives: still enabled, and the pack above the cutoff.
 *
 * @return Empty when fine, else the reason to disarm
 */
static const char* checkDrives() {
    static char reason[64];

    for (Track* track : {&leftTrack, &rightTrack}) {
        DriveState state = readState(*track);

        if (state != DriveState::OPERATION_ENABLED) {
            uint32_t errorCode = 0;
            readObject(*track, Objects::ERROR_CODE, errorCode);
            snprintf(reason, sizeof(reason), "%s drive left Operation Enabled, error 0x%04" PRIX32, track->name, errorCode);

            return reason;
        }

        uint32_t busVoltageDeciv = 0;

        if (readObject(*track, Objects::BUS_VOLTAGE, busVoltageDeciv)) {
            track->busVoltageDeciv = static_cast<int32_t>(busVoltageDeciv);
        }
    }

    // D-005 cutoff on the lower of the two readings
    int32_t lowestDeciv = std::min(leftTrack.busVoltageDeciv, rightTrack.busVoltageDeciv);

    if (lowestDeciv > 0 && lowestDeciv < BoardConfig::PACK_CUTOFF_DECIV) {
        snprintf(reason, sizeof(reason), "pack at %" PRId32 ".%" PRId32 " V, below the cutoff", lowestDeciv / 10, lowestDeciv % 10);

        return reason;
    }

    return "";
}

// Mode changes

static void turnOn() {
    setStatus("Taking the bus from the motor service");
    motor->setBusSuspended(true);
    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(BUS_HANDOVER_TIMEOUT_MS) * 1000;

    while (!motor->isBusSuspended()) {
        if (esp_timer_get_time() > deadlineUs) {
            motor->setBusSuspended(false);
            setStatus("The motor service didn't hand over the bus");

            return;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Both drives answer and are off
    bool isLeftPresent = readState(leftTrack) != DriveState::UNKNOWN;
    bool isRightPresent = readState(rightTrack) != DriveState::UNKNOWN;
    writeObject(leftTrack, Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "power stage off");
    writeObject(rightTrack, Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "power stage off");
    gamepadTestSetTapHandler(tankTestStop);
    tankState = TankState::DISARMED;

    if (!isLeftPresent || !isRightPresent) {
        setStatus("Tank on, but node %u (left) %s and node %u (right) %s",
                  leftTrack.nodeId.load(),
                  isLeftPresent ? "answers" : "is missing",
                  rightTrack.nodeId.load(),
                  isRightPresent ? "answers" : "is missing");

        return;
    }

    setStatus("Disarmed: hold LB + RB to arm");
}

static void turnOff() {
    if (tankState == TankState::ARMED) {
        disarm("tank off");
    }

    gamepadTestSetTapHandler(nullptr);
    tankState = TankState::OFF;
    motor->setBusSuspended(false);
    setStatus("Tank off");
    gamepadTestSetSecondStatus("");
}

// Task

static void tankLoop(void*) {
    int64_t armHoldStartUs = 0;
    bool wasYPressed = false;
    uint32_t loopCount = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
        loopCount++;

        if (isOffRequested.exchange(false)) {
            isOnRequested = false;

            if (tankState != TankState::OFF) {
                turnOff();
            }

            continue;
        }

        if (isOnRequested.exchange(false) && tankState == TankState::OFF) {
            turnOn();

            continue;
        }

        if (tankState == TankState::OFF) {
            isStopRequested = false;

            continue;
        }

        GamepadInput input;
        gamepadTestGetInput(input);

        // Y toggles fast mode, on the press only; it takes effect at once, armed or not
        bool isYPressed = input.isConnected && (input.buttons & GAMEPAD_BUTTON_Y) != 0;

        if (isYPressed && !wasYPressed) {
            isFast = !isFast;
            ESP_LOGI(TAG, "Fast mode %s", isFast ? "on" : "off");

            if (tankState == TankState::DISARMED) {
                setStatus(isFast ? "Disarmed (fast): hold LB + RB to arm" : "Disarmed: hold LB + RB to arm");
            }
        }

        wasYPressed = isYPressed;

        // Disarmed: arm on LB + RB held, sticks centred
        if (tankState == TankState::DISARMED) {
            isStopRequested = false;
            bool isArmHeld = input.isConnected && (input.buttons & GAMEPAD_BUTTON_LB) != 0 && (input.buttons & GAMEPAD_BUTTON_RB) != 0 && areSticksCentred(input);

            if (!isArmHeld) {
                armHoldStartUs = 0;

                continue;
            }

            if (armHoldStartUs == 0) {
                armHoldStartUs = esp_timer_get_time();
            }

            if (esp_timer_get_time() - armHoldStartUs < ARM_HOLD_US) {
                continue;
            }

            armHoldStartUs = 0;
            setStatus("Arming");

            if (!armTrack(leftTrack) || !armTrack(rightTrack)) {
                // armTrack set the reason; make sure neither stays enabled
                char reason[96];
                snprintf(reason, sizeof(reason), "%s", statusText);
                disarm(reason);

                continue;
            }

            tankState = TankState::ARMED;
            loopCount = 0;
            setStatus("Armed: left stick drives, right stick turns, B disarms");

            continue;
        }

        // Armed: stops first
        if (isStopRequested.exchange(false)) {
            disarm("STOP");

            continue;
        }

        if (!input.isConnected) {
            disarm("controller lost");

            continue;
        }

        if ((input.buttons & GAMEPAD_BUTTON_B) != 0) {
            disarm("B");

            continue;
        }

        if (loopCount % CHECK_EVERY_LOOPS == 0) {
            const char* reason = checkDrives();

            if (reason[0] != '\0') {
                disarm(reason);

                continue;
            }
        }

        // Drive
        int32_t leftRpm = 0;
        int32_t rightRpm = 0;
        mixSticks(input, leftRpm, rightRpm);
        bool shouldRefresh = loopCount % REFRESH_EVERY_LOOPS == 0;
        writeTarget(leftTrack, leftRpm, shouldRefresh);
        writeTarget(rightTrack, rightRpm, shouldRefresh);

        if (shouldRefresh) {
            int32_t voltageDeciv = std::min(leftTrack.busVoltageDeciv, rightTrack.busVoltageDeciv);
            snprintf(statusText,
                     sizeof(statusText),
                     "Armed%s  L %" PRId32 "  R %" PRId32 " rpm  %" PRId32 ".%" PRId32 " V  (B disarms, Y fast)",
                     isFast ? " FAST" : "",
                     leftRpm,
                     rightRpm,
                     voltageDeciv / 10,
                     voltageDeciv % 10);
            gamepadTestSetSecondStatus(statusText);
        }
    }
}

// Public API

void tankTestBegin(SdoClient& sdoClient, MotorService& motorService) {
    sdo = &sdoClient;
    motor = &motorService;
}

void tankTestOn() {
    if (sdo == nullptr) {
        ESP_LOGE(TAG, "tankTestBegin() wasn't called");

        return;
    }

    // Stack in PSRAM: internal RAM is nearly full, and this task never writes flash
    if (taskHandle == nullptr && xTaskCreateWithCaps(tankLoop, "tank", 6144, nullptr, 4, &taskHandle, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Starting the tank task failed");

        return;
    }

    isOnRequested = true;
}

void tankTestOff() {
    isOffRequested = true;
}

void tankTestStop() {
    isStopRequested = true;
}

void tankTestSetNodes(uint8_t leftNodeId, uint8_t rightNodeId) {
    if (tankState == TankState::ARMED) {
        ESP_LOGW(TAG, "Disarm before changing nodes: the old drive would keep its last target");

        return;
    }

    if (leftNodeId == rightNodeId) {
        ESP_LOGW(TAG, "Left and right need different nodes");

        return;
    }

    leftTrack.nodeId = leftNodeId;
    rightTrack.nodeId = rightNodeId;
    tankTestLogStatus();
}

void tankTestToggleInvert(bool isLeft) {
    Track& track = isLeft ? leftTrack : rightTrack;
    track.isInverted = !track.isInverted;
    tankTestLogStatus();
}

void tankTestSetMaxRpm(int32_t rpm) {
    maxRpm = std::clamp<int32_t>(rpm, 10, MAX_RPM_LIMIT);
    tankTestLogStatus();
}

void tankTestSetTurnPercent(int32_t percent) {
    turnPercent = std::clamp<int32_t>(percent, 10, 100);
    tankTestLogStatus();
}

void tankTestSetAccelerationRpmPerSecond(int32_t rpmPerSecond) {
    accelerationRpmPerSecond = std::clamp<int32_t>(rpmPerSecond, 50, 20000);
    tankTestLogStatus();
}

void tankTestLogStatus() {
    static const char* STATE_NAMES[] = {"off", "disarmed", "armed"};

    ESP_LOGI(TAG,
             "State %s; left node %u%s, right node %u%s; max %" PRId32 " rpm%s, turn %" PRId32 " %%, acceleration %" PRId32 " rpm/s",
             STATE_NAMES[static_cast<uint8_t>(tankState.load())],
             leftTrack.nodeId.load(),
             leftTrack.isInverted ? " inverted" : "",
             rightTrack.nodeId.load(),
             rightTrack.isInverted ? " inverted" : "",
             maxRpm.load(),
             isFast ? " (fast mode: doubled)" : "",
             turnPercent.load(),
             accelerationRpmPerSecond.load());
}
