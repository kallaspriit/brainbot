#include "MotorService.hpp"

#include <algorithm>
#include <cinttypes>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "BoardConfig.hpp"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "protocol/AlarmCodes.hpp"
#include "protocol/AlarmWord.hpp"
#include "protocol/FactoryDump.hpp"
#include "protocol/ParameterFormat.hpp"

static const char* TAG = "MotorService";

MotorService::MotorService(SdoClient& sdo, CanBus& bus)
    : sdo(sdo)
    , bus(bus) {
}

bool MotorService::begin() {
    commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(Command));

    if (commandQueue == nullptr) {
        return false;
    }

    sdo.setOtherFrameHandler([this](const CanFrame& frame) {
        handleFrame(frame);
    });

    return xTaskCreate(taskMain, "motor", TASK_STACK_BYTES, this, TASK_PRIORITY, &taskHandle) == pdPASS;
}

void MotorService::requestStartVelocity() {
    Command command = Command::START_VELOCITY;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestStop() {
    isStopRequested = true;
}

void MotorService::requestFaultReset() {
    Command command = Command::FAULT_RESET;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestReadParameters(bool shouldLog) {
    shouldLogParameters = shouldLog;
    requestedParameterGroup = ALL_PARAMETERS;
}

void MotorService::requestReadParameterGroup(uint8_t group) {
    requestedParameterGroup = group;
}

size_t MotorService::getParameterValues(ParameterValue* values, size_t maxCount) {
    std::lock_guard<std::mutex> lock(statusMutex);
    size_t count = std::min(maxCount, Parameters::ENTRY_COUNT);
    std::copy(parameterValues, parameterValues + count, values);

    return count;
}

void MotorService::requestWriteParameter(size_t position, uint32_t raw) {
    std::lock_guard<std::mutex> lock(statusMutex);
    hasPendingWrite = position < Parameters::ENTRY_COUNT;
    pendingWritePosition = position;
    pendingWriteRaw = raw;
}

size_t MotorService::getUnsavedChanges(UnsavedChange* changes, size_t maxCount) {
    std::lock_guard<std::mutex> lock(statusMutex);
    size_t count = std::min<size_t>(maxCount, status.unsavedChangeCount);
    std::copy(unsavedChanges, unsavedChanges + count, changes);

    return count;
}

void MotorService::requestSave() {
    Command command = Command::SAVE;
    xQueueSend(commandQueue, &command, 0);
}

SavePreview MotorService::getSavePreview() {
    std::lock_guard<std::mutex> lock(statusMutex);
    SavePreview preview;
    preview.usesFactoryForOwn = status.hasEarlierSessionValues;

    const Parameters::Entry* undervoltage = Parameters::find(Objects::BUS_UNDERVOLTAGE_LIMIT_V.index, Objects::BUS_UNDERVOLTAGE_LIMIT_V.subIndex);
    const Parameters::Entry* rated = Parameters::find(Objects::RATED_VOLTAGE.index, Objects::RATED_VOLTAGE.subIndex);

    // The undervoltage limit goes back only if the bus is above it, or the drive would raise undervoltage at once
    if (undervoltage != nullptr) {
        const ParameterValue& value = parameterValues[undervoltage - Parameters::ENTRIES];
        uint32_t ownV = preview.usesFactoryForOwn || !value.hasOwnValue ? static_cast<uint32_t>(undervoltage->factory) : value.ownRaw;
        preview.ownUndervoltageV = ownV;
        preview.batteryUndervoltageV = UNDERVOLTAGE_LIMIT_FOR_BATTERY_V;
        preview.keepsBatteryUndervoltage = status.busVoltageDeciV <= static_cast<int32_t>(ownV) * 10;
    }

    // Flash writes need the bus at about 80 % of 2000:07 (D-025)
    if (rated != nullptr) {
        const ParameterValue& value = parameterValues[rated - Parameters::ENTRIES];
        preview.ratedVoltageV = value.isRead ? value.raw : static_cast<uint32_t>(rated->factory);
        preview.loweredRatedVoltageV = RESTORE_RATED_VOLTAGE_V;
        preview.flashMinimumDeciV = preview.ratedVoltageV * FLASH_WRITE_MIN_PERCENT / 10;
        preview.lowersRatedVoltage = status.busVoltageDeciV < static_cast<int32_t>(preview.ratedVoltageV) * FLASH_WRITE_MIN_PERCENT / 10 && preview.ratedVoltageV > RESTORE_RATED_VOLTAGE_V;
    }

    return preview;
}

void MotorService::requestRevert() {
    Command command = Command::REVERT;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestDump() {
    Command command = Command::DUMP;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestRestoreDefaults() {
    Command command = Command::RESTORE_DEFAULTS;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestSearch() {
    isSearchRequested = true;
}

void MotorService::setBusSuspended(bool shouldSuspend) {
    isSuspendRequested = shouldSuspend;
}

bool MotorService::isBusSuspended() const {
    return isSuspended;
}

void MotorService::requestRecover() {
    Command command = Command::RECOVER;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestBlankForTest() {
    Command command = Command::BLANK_FOR_TEST;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestCalibration() {
    Command command = Command::CALIBRATE;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestFakeBlank() {
    isFakeBlank = true;

    // Visible at once, not only after the next connect
    std::lock_guard<std::mutex> lock(statusMutex);
    status.runChecks.isParameterFlashBlank = true;
    status.faultRecordVersion++;
    ESP_LOGW(TAG, "Faking a blank parameter flash until the console restarts");
}

void MotorService::requestRawSdo(const RawSdoRequest& request) {
    // Safety rules: 200C:01 is never written, and the save and restore objects take only their documented commands
    if (request.kind == RawSdoRequest::Kind::WRITE) {
        bool isCommunicationSelect = request.index == 0x200C && request.subIndex == 0x01;
        bool isSave = request.index == Objects::SAVE_PARAMETERS.index;
        bool isRestore = request.index == Objects::RESTORE_DEFAULTS.index;
        bool isDocumentedSave = isSave && request.subIndex == Objects::SAVE_PARAMETERS.subIndex && request.value == SAVE_COMMAND_SAVE;
        bool isDocumentedRestore = isRestore && request.subIndex == Objects::RESTORE_DEFAULTS.subIndex && request.value == RESTORE_COMMAND_LOAD;

        if (isCommunicationSelect || ((isSave || isRestore) && !isDocumentedSave && !isDocumentedRestore)) {
            ESP_LOGW(TAG, "SDO write %04X:%02X refused by the safety rules", request.index, request.subIndex);

            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(rawSdoMutex);
        rawSdoRequest = request;
    }

    Command command = Command::RAW_SDO;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestRampStop() {
    Command command = Command::RAMP_STOP;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::setTargetVelocityRpm(int32_t velocityRpm) {
    int32_t limitRpm = velocityLimitRpm;
    targetVelocityRpm = std::clamp(velocityRpm, -limitRpm, limitRpm);
}

void MotorService::setAccelerationRpmPerSecond(int32_t accelerationRpmPerSecond) {
    targetAccelerationRpmPerSecond = std::clamp(accelerationRpmPerSecond, MIN_ACCELERATION_RPM_PER_SECOND, static_cast<int32_t>(accelerationLimitRpmPerSecond));
}

void MotorService::setPositionTargetMilliturns(int32_t targetMilliturns) {
    targetPositionMilliturns = std::clamp(targetMilliturns, -MAX_POSITION_TARGET_TURNS * 1000, MAX_POSITION_TARGET_TURNS * 1000);
}

void MotorService::setMoveSpeedRpm(int32_t speedRpm) {
    moveSpeedRpm = std::clamp<int32_t>(speedRpm, 1, velocityLimitRpm);
}

void MotorService::setPositionRelative(bool isRelative) {
    isPositionRelative = isRelative;
}

void MotorService::setMoveByMilliturns(int32_t distanceMilliturns) {
    moveByMilliturns = std::clamp(distanceMilliturns, -MAX_POSITION_TARGET_TURNS * 1000, MAX_POSITION_TARGET_TURNS * 1000);
}

void MotorService::requestGoPosition() {
    Command command = Command::GO_POSITION;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestZeroHere() {
    Command command = Command::ZERO_HERE;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestRelease() {
    Command command = Command::RELEASE;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestMotionScreen(MotionMode mode) {
    Command command = Command::ENTER_VELOCITY_SCREEN;

    if (mode == MotionMode::POSITION) {
        command = Command::ENTER_POSITION_SCREEN;
    } else if (mode == MotionMode::TORQUE) {
        command = Command::ENTER_TORQUE_SCREEN;
    }

    xQueueSend(commandQueue, &command, 0);
}

uint32_t MotorService::getStackHighWaterBytes() const {
    // ESP-IDF counts the high-water mark in bytes
    return taskHandle != nullptr ? uxTaskGetStackHighWaterMark(taskHandle) : 0;
}

void MotorService::requestStartTorque() {
    Command command = Command::START_TORQUE;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestTorqueStop() {
    Command command = Command::TORQUE_STOP;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::setTargetTorquePerMille(int32_t torquePerMille) {
    targetTorquePerMille = std::clamp(torquePerMille, -MAX_TORQUE_PER_MILLE, MAX_TORQUE_PER_MILLE);
}

void MotorService::setTorqueSlopePerMillePerSecond(int32_t slopePerMillePerSecond) {
    torqueSlopePerMillePerSecond = std::max<int32_t>(slopePerMillePerSecond, 1);
}

void MotorService::setStepTestPosition(bool isPosition) {
    isStepTestPosition = isPosition;
}

void MotorService::setStepTestVelocities(int32_t velocityARpm, int32_t velocityBRpm) {
    int32_t limitRpm = velocityLimitRpm;
    stepTestVelocityARpm = std::clamp(velocityARpm, -limitRpm, limitRpm);
    stepTestVelocityBRpm = std::clamp(velocityBRpm, -limitRpm, limitRpm);
}

void MotorService::setStepTestStepMilliturns(int32_t stepMilliturns) {
    stepTestStepMilliturns = std::clamp(stepMilliturns, -MAX_POSITION_TARGET_TURNS * 1000, MAX_POSITION_TARGET_TURNS * 1000);
}

void MotorService::setStepTestDwellMs(int32_t dwellMs) {
    stepTestDwellMs = std::clamp(dwellMs, MIN_STEP_TEST_DWELL_MS, MAX_STEP_TEST_DWELL_MS);
}

void MotorService::setStepTestAccelerationRpmPerSecond(int32_t accelerationRpmPerSecond) {
    stepTestAccelerationRpmPerSecond = std::clamp(accelerationRpmPerSecond, MIN_ACCELERATION_RPM_PER_SECOND, MAX_STEP_TEST_ACCELERATION_RPM_PER_SECOND);
}

void MotorService::setStepTestMoveSpeedRpm(int32_t speedRpm) {
    stepTestMoveSpeedRpm = std::clamp<int32_t>(speedRpm, 1, velocityLimitRpm);
}

int32_t MotorService::getStepTestAccelerationRpmPerSecond() const {
    int32_t set = stepTestAccelerationRpmPerSecond;

    // Not set: the steepest the test allows, so the step shows the loop and not the ramp
    return set == NOT_SET ? MAX_STEP_TEST_ACCELERATION_RPM_PER_SECOND : set;
}

int32_t MotorService::getStepTestMoveSpeedRpm() const {
    int32_t set = stepTestMoveSpeedRpm;

    // Not set: the speed limit (rated speed), so the profile doesn't hide the loop
    return set == NOT_SET ? velocityLimitRpm.load() : std::min<int32_t>(set, velocityLimitRpm);
}

void MotorService::requestStartStepTest() {
    Command command = Command::START_STEP_TEST;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestStopStepTest() {
    Command command = Command::STOP_STEP_TEST;
    xQueueSend(commandQueue, &command, 0);
}

void MotorService::requestHalt() {
    Command command = Command::HALT;
    xQueueSend(commandQueue, &command, 0);
}

MotorStatus MotorService::getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);
    MotorStatus snapshot = status;

    // Targets live in atomics set by the UI; read them here so the snapshot is always current
    snapshot.targetVelocityRpm = targetVelocityRpm;
    snapshot.targetAccelerationRpmPerSecond = targetAccelerationRpmPerSecond;
    snapshot.velocityLimitRpm = velocityLimitRpm;
    snapshot.accelerationLimitRpmPerSecond = accelerationLimitRpmPerSecond;
    snapshot.targetPositionMilliturns = targetPositionMilliturns;
    snapshot.moveSpeedRpm = moveSpeedRpm;
    snapshot.isPositionRelative = isPositionRelative;
    snapshot.targetTorquePerMille = targetTorquePerMille;
    snapshot.torqueSlopePerMillePerSecond = torqueSlopePerMillePerSecond;
    snapshot.isStepTestPosition = isStepTestPosition;
    snapshot.stepTestVelocityARpm = stepTestVelocityARpm;
    snapshot.stepTestVelocityBRpm = stepTestVelocityBRpm;
    snapshot.stepTestStepMilliturns = stepTestStepMilliturns;
    snapshot.stepTestDwellMs = stepTestDwellMs;
    snapshot.stepTestAccelerationRpmPerSecond = getStepTestAccelerationRpmPerSecond();
    snapshot.stepTestMoveSpeedRpm = getStepTestMoveSpeedRpm();
    snapshot.moveByMilliturns = moveByMilliturns;

    // GO wouldn't move the shaft (the screen shows it as an outline): an absolute target, converted as goPosition()
    // does, equal to the one the drive is moving to or holding; or, with the drive released, equal to the position as
    // shown (GO would then only lock the shaft where it is)
    bool isFollowingTarget = snapshot.isRunning && snapshot.motionMode == MotionMode::POSITION && !snapshot.isHalted;
    bool isReleased = !snapshot.isRunning || (snapshot.motionMode == MotionMode::POSITION && snapshot.isHalted);

    if (!snapshot.isPositionRelative && snapshot.unitsPerTurn > 0) {
        if (isFollowingTarget) {
            int64_t targetUnits = snapshot.positionZeroUnits + static_cast<int64_t>(snapshot.targetPositionMilliturns) * snapshot.unitsPerTurn / 1000;
            snapshot.isGoWithoutEffect = targetUnits == snapshot.sentTargetUnits;
        } else if (isReleased) {
            snapshot.isGoWithoutEffect = snapshot.positionMilliturns == snapshot.targetPositionMilliturns;
        }
    }

    return snapshot;
}

void MotorService::taskMain(void* context) {
    static_cast<MotorService*>(context)->run();
}

void MotorService::run() {
    // A hung motor task resets the console (D7); the connect sequence then stops a drive left running
    esp_task_wdt_add(nullptr);

    while (true) {
        // THROWAWAY (test/gamepad): the bus is lent out. Stop and forget the drive once, park the bus at the default,
        // then only feed the task watchdog: keepAlive() could write to the drive's watchdog over the borrowed client
        if (isSuspendRequested) {
            if (!isSuspended) {
                if (getStatus().isRunning) {
                    stopMotion("Stopped: bus handed to the tank test");
                }

                {
                    std::lock_guard<std::mutex> lock(statusMutex);
                    status.isConnected = false;
                }

                searchStep = 0;
                defaultMissCount = 0;
                applySearchStep(searchStep);
                isSuspended = true;
                ESP_LOGI(TAG, "Bus suspended");
            }

            // The borrower doesn't own the controller, so bus-off recovery stays here (it only touches the controller)
            bus.recoverIfBusOff();
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(50));

            continue;
        }

        if (isSuspended) {
            isSuspended = false;
            shouldResetSearch = true;
            ESP_LOGI(TAG, "Bus resumed");
        }

        keepAlive();

        // CAN controller health
        twai_node_status_t canStatus = {};
        bus.getStatus(canStatus);
        bus.recoverIfBusOff();

        {
            std::lock_guard<std::mutex> lock(statusMutex);
            status.canErrorState = canStatus.state;
            status.canTransmitErrors = canStatus.tx_error_count;
            status.canReceiveErrors = canStatus.rx_error_count;
            status.canReceiveDrops = bus.getReceiveDropCount();
        }

        // Search now: forget the drive and start past the default (D-026), never under a running motor
        if (isSearchRequested) {
            isSearchRequested = false;

            if (!getStatus().isRunning) {
                {
                    std::lock_guard<std::mutex> lock(statusMutex);
                    status.isConnected = false;
                }

                searchStep = 1;
                applySearchStep(searchStep);
                ESP_LOGI(TAG, "Search: starting past the default");
            }
        }

        // After a lost drive, the search starts again at the default
        if (shouldResetSearch) {
            shouldResetSearch = false;
            searchStep = 0;
            defaultMissCount = 0;
            applySearchStep(searchStep);
        }

        // Find the drive; connecting switches it off, which satisfies any stop asked for meanwhile. Without an answer at
        // the default for a few seconds, try the other bitrates and node IDs one step per loop (D-026)
        if (!getStatus().isConnected) {
            if (!connect()) {
                if (searchStep == 0 && ++defaultMissCount < DEFAULT_MISSES_BEFORE_SEARCH) {
                    vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));

                    continue;
                }

                defaultMissCount = 0;
                searchStep = (searchStep + 1) % SEARCH_STEP_COUNT;
                applySearchStep(searchStep);

                // Back at the default after a full sweep: count it, wait as normal. On an empty bus the sweep takes a
                // fraction of a second (no ACK, every try fails at once), so the screen shows sweeps, not steps
                if (searchStep == 0) {
                    {
                        std::lock_guard<std::mutex> lock(statusMutex);
                        status.searchRoundCount++;
                    }

                    vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
                }

                continue;
            }

            // Found: normal timeouts from now on, at the node and bitrate that answered
            sdo.setTimeoutMs(BoardConfig::SDO_TIMEOUT_MS);
            MotorStatus found = getStatus();

            {
                std::lock_guard<std::mutex> lock(statusMutex);
                status.searchRoundCount = 0;
            }

            if (found.busNodeId != BoardConfig::DRIVE_NODE_ID || found.busBitrate != BoardConfig::CAN_BITRATE) {
                setMessage(MessageLevel::WARNING, "Drive found at node %u, %" PRIu32 " kbit/s, not the default node 1 at 1 Mbit/s", found.busNodeId, found.busBitrate / 1000);
                ESP_LOGW(TAG, "Drive found at node %u, %" PRIu32 " bit/s", found.busNodeId, found.busBitrate);
            }

            isStopRequested = false;
            xQueueReset(commandQueue);
        }

        // STOP first; commands queued before it are dropped
        if (isStopRequested.exchange(false)) {
            xQueueReset(commandQueue);
            stopMotion("Stopped");
        }

        // One queued command per cycle, so polling and STOP stay responsive
        Command command;

        if (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
            switch (command) {
                case Command::START_VELOCITY:
                    startVelocity();
                    break;

                case Command::RAMP_STOP:
                    rampStop();
                    break;

                case Command::GO_POSITION:
                    goPosition();
                    break;

                case Command::HALT:
                    haltPosition();
                    break;

                case Command::RELEASE:
                    releasePosition();
                    break;

                case Command::ENTER_VELOCITY_SCREEN:
                    endOtherMode(MotionMode::VELOCITY, "Velocity screen opened");
                    break;

                case Command::ENTER_POSITION_SCREEN:
                    endOtherMode(MotionMode::POSITION, "Position screen opened");
                    break;

                case Command::ZERO_HERE:
                    zeroHere();
                    break;

                case Command::ENTER_TORQUE_SCREEN:
                    endOtherMode(MotionMode::TORQUE, "Torque screen opened");
                    break;

                case Command::FAULT_RESET:
                    resetFault();
                    break;

                case Command::DUMP:
                    dumpObjects();
                    break;

                case Command::RAW_SDO:
                    performRawSdo();
                    break;

                case Command::RECOVER:
                    recoverBlankFlash();
                    break;

                case Command::BLANK_FOR_TEST:
                    blankParametersForTest();
                    break;

                case Command::CALIBRATE:
                    runCalibration();
                    break;

                case Command::RESTORE_DEFAULTS:
                    restoreDefaults();
                    break;

                case Command::SAVE:
                    saveParameters();
                    break;

                case Command::REVERT:
                    revertParameters();
                    break;

                case Command::START_TORQUE:
                    startTorque();
                    break;

                case Command::TORQUE_STOP:
                    torqueStop();
                    break;

                case Command::START_STEP_TEST:
                    startStepTest();
                    break;

                case Command::STOP_STEP_TEST:
                    stopStepTest();
                    break;
            }
        }

        // The step test's next step, or its end when the motor stopped (PAR-010)
        runStepTest();

        // A stack running low says so before it overflows
        if (!hasWarnedStack && uxTaskGetStackHighWaterMark(nullptr) < STACK_WARNING_BYTES) {
            hasWarnedStack = true;
            ESP_LOGW(TAG, "Motor task stack low: %u bytes left of %u", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned>(TASK_STACK_BYTES));
        }

        // Follow target changes while running; the velocity target only means something in velocity mode
        MotorStatus running = getStatus();

        if (running.isRunning && running.motionMode == MotionMode::POSITION) {
            followPositionProfile(running);
        } else if (running.isRunning && running.motionMode == MotionMode::TORQUE) {
            followTorque();
        } else if (running.isRunning) {
            int32_t accelerationRpmPerSecond = targetAccelerationRpmPerSecond;

            if (accelerationRpmPerSecond != appliedAccelerationRpmPerSecond) {
                applyAcceleration();
            }

            int32_t velocityRpm = targetVelocityRpm;

            if (running.motionMode == MotionMode::VELOCITY && velocityRpm != appliedVelocityRpm &&
                writeObject(Objects::TARGET_VELOCITY, static_cast<uint32_t>(rpmToCountsPerSecond(velocityRpm)), "Target velocity")) {
                appliedVelocityRpm = velocityRpm;
            }
        }

        // Refresh the status, and give up on the drive after repeated failures
        if (poll()) {
            failedPollCount = 0;
            checkPackVoltage();
            checkStall();
            checkSettled();

            // The history after an EMCY, a changed error code or a reset
            if (isFaultRecordStale) {
                readFaultRecord();
            }

            // A parameter write waits for a good poll too (PAR-004)
            bool hasWrite = false;
            size_t writePosition = 0;
            uint32_t writeRaw = 0;

            {
                std::lock_guard<std::mutex> lock(statusMutex);
                hasWrite = hasPendingWrite;
                writePosition = pendingWritePosition;
                writeRaw = pendingWriteRaw;
                hasPendingWrite = false;
            }

            if (hasWrite) {
                writeParameter(writePosition, writeRaw);
            }

            // A parameter read: start a requested one, then a chunk per pass (PAR-002)
            int32_t requestedGroup = requestedParameterGroup.exchange(NO_PARAMETER_READ);

            if (requestedGroup != NO_PARAMETER_READ) {
                startParameterRead(requestedGroup);
            }

            if (isParameterReadActive) {
                readParameterChunk();
            }
        } else if (++failedPollCount >= MAX_FAILED_POLLS) {
            handleLostDrive();
        }

        sampleUntilNextPoll();
    }
}

bool MotorService::connect() {
    uint32_t vendorId = 0;
    SdoResult result = sdo.read(Objects::VENDOR_ID, vendorId);

    if (result != SdoResult::OK) {
        // Only at the default; the search's misses would flood the message line
        if (searchStep == 0) {
            setMessage(MessageLevel::INFO, "No drive at node %d: %s", BoardConfig::DRIVE_NODE_ID, SdoClient::getResultName(result));
        }

        return false;
    }

    // What the drive recorded about faults and how it came, before anything is written to it (D-024)
    readRunCheckValues();
    readFaultRecord();
    captureOwnParameterValues();

    // A recovered drive that reads blank again was power-cycled: the table's values are gone with it (D-031). A drive
    // that only lost the bus still holds them, so the mark stays. The power-cycle wait is over at any reconnect: a
    // drive that merely lost the bus keeps the same run-time state it was running with before
    {
        std::lock_guard<std::mutex> lock(statusMutex);

        if (status.runChecks.isParameterFlashBlank) {
            status.isRecoveredNotRestored = false;
        }

        status.needsPowerCycle = false;

        // A calibration's result belongs to the session before the power cycle; the table shows the stored values again
        status.calibration = CalibrationStatus {};
        status.calibrationVersion++;
    }
    clearUnsavedChanges("the drive reconnected, and a power cycle loses them");

    // Identity and configuration; best effort, the vendor read already showed the drive is there
    uint32_t productCode = 0;
    uint32_t serialNumber = 0;
    uint32_t nodeId = 0;
    uint32_t bitrateCode = 0;
    uint32_t increments = 0;
    uint32_t quickStopDeceleration = 0;
    int32_t quickStopOptionCode = 0;
    uint32_t maxProfileVelocity = 0;
    uint32_t watchdogValue = 0;
    uint32_t ratedCurrentCentiamps = 0;
    uint32_t ratedTorqueCentiNm = 0;
    uint32_t statorResistanceMilliohms = 0;
    uint32_t ratedSpeedRpm = 0;
    uint32_t maxMotorSpeedRpm = 0;
    uint32_t encoderMotorRevolutions = 0;
    uint32_t gearMotorRevolutions = 0;
    uint32_t gearShaftRevolutions = 0;
    int32_t positionLimitMin = 0;
    int32_t positionLimitMax = 0;
    uint32_t followingErrorWindow = 0;
    uint32_t positionWindow = 0;
    uint32_t positioningOptionCode = 0;
    int32_t positionUnits = 0;
    sdo.read(Objects::PRODUCT_CODE, productCode);
    sdo.read(Objects::SERIAL_NUMBER, serialNumber);
    sdo.read(Objects::NODE_ID, nodeId);
    sdo.read(Objects::BITRATE, bitrateCode);
    sdo.read(Objects::ENCODER_INCREMENTS, increments);
    keepAlive();
    sdo.read(Objects::QUICK_STOP_DECELERATION, quickStopDeceleration);
    sdo.readSigned(Objects::QUICK_STOP_OPTION_CODE, quickStopOptionCode);
    sdo.read(Objects::MAX_PROFILE_VELOCITY, maxProfileVelocity);
    sdo.read(Objects::COMMUNICATION_WATCHDOG_MS, watchdogValue);
    sdo.read(Objects::RATED_CURRENT, ratedCurrentCentiamps);
    sdo.read(Objects::RATED_TORQUE, ratedTorqueCentiNm);
    sdo.read(Objects::STATOR_RESISTANCE, statorResistanceMilliohms);
    sdo.read(Objects::RATED_SPEED, ratedSpeedRpm);
    sdo.read(Objects::MAX_MOTOR_SPEED, maxMotorSpeedRpm);
    keepAlive();
    sdo.read(Objects::ENCODER_MOTOR_REVOLUTIONS, encoderMotorRevolutions);
    sdo.read(Objects::GEAR_MOTOR_REVOLUTIONS, gearMotorRevolutions);
    sdo.read(Objects::GEAR_SHAFT_REVOLUTIONS, gearShaftRevolutions);
    sdo.readSigned(Objects::SOFTWARE_POSITION_LIMIT_MIN, positionLimitMin);
    sdo.readSigned(Objects::SOFTWARE_POSITION_LIMIT_MAX, positionLimitMax);
    sdo.read(Objects::FOLLOWING_ERROR_WINDOW, followingErrorWindow);
    sdo.read(Objects::POSITION_WINDOW, positionWindow);
    sdo.read(Objects::POSITIONING_OPTION_CODE, positioningOptionCode);
    sdo.readSigned(Objects::POSITION_ACTUAL_USER, positionUnits);

    // User units per motor turn: encoder resolution over gear ratio (CN:6611: rpm = 6081 x gear / resolution x 60)
    int64_t unitsPerTurn = 0;

    if (encoderMotorRevolutions > 0 && gearMotorRevolutions > 0) {
        unitsPerTurn = static_cast<int64_t>(increments) * gearShaftRevolutions / (static_cast<int64_t>(encoderMotorRevolutions) * gearMotorRevolutions);
    }

    // A drive with the communication watchdog on must be fed from now on; it is read, never written (D7)
    watchdogMs = watchdogValue;
    lastWatchdogFeedUs = 0;
    encoderIncrements = increments;
    failedPollCount = 0;
    lowVoltagePollCount = 0;
    appliedVelocityRpm = 0;
    appliedAccelerationRpmPerSecond = 0;
    keepAlive();

    bool wasLostWhileRunning = false;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        wasLostWhileRunning = status.isLostWhileRunning;
        status.isConnected = true;
        status.isRunning = false;
        status.vendorId = vendorId;
        status.productCode = productCode;
        status.serialNumber = serialNumber;
        status.nodeId = nodeId;
        status.bitrateCode = bitrateCode;
        status.encoderIncrements = increments;
        status.quickStopDecelerationCounts = quickStopDeceleration;
        status.quickStopOptionCode = quickStopOptionCode;
        status.maxProfileVelocityCounts = maxProfileVelocity;
        status.ratedCurrentCentiamps = ratedCurrentCentiamps;
        status.ratedTorqueMilliNm = static_cast<int32_t>(ratedTorqueCentiNm * 10);
        status.statorResistanceMilliohms = statorResistanceMilliohms;
        status.ratedSpeedRpm = ratedSpeedRpm;
        status.maxMotorSpeedRpm = maxMotorSpeedRpm;
        status.watchdogMs = watchdogMs;
        status.unitsPerTurn = static_cast<int32_t>(unitsPerTurn);
        status.positionUnits = positionUnits;
        status.positionZeroUnits = positionUnits; // Zero at connect; POS-005 keeps it per motor
        status.positionLimitMinUnits = positionLimitMin;
        status.positionLimitMaxUnits = positionLimitMax;
        status.originalPositionLimitMinUnits = positionLimitMin;
        status.originalPositionLimitMaxUnits = positionLimitMax;
        status.motionMode = MotionMode::NONE;
        status.isHalted = false;
    }

    updateLimits(ratedSpeedRpm, maxMotorSpeedRpm, maxProfileVelocity, quickStopDeceleration);
    readTorqueSpeedLimits();
    readIdentityStrings();

    // A drive may still be running or holding from before a console reset or a pulled cable (D7): brake it and switch
    // it off. With the heartbeat on, a lost bus makes it halt itself and hold, so "enabled but still" is the usual case
    poll();
    MotorStatus current = getStatus();

    // Only a powered drive can be running or holding; with the power stage off a speed reading is noise (2 rpm seen in
    // Ready To Switch On right after power-on, bench 2026-10-11) or a shaft coasting with nothing to brake it
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;
    bool isMoving = isPowered && std::abs(current.velocityRpm) >= STOPPED_THRESHOLD_RPM;
    bool wasRunning = isPowered;

    ESP_LOGI(TAG, "At connect: statusword 0x%04X (%s), 606C %" PRId32 " rpm", current.statusword, getDriveStateName(current.state), current.velocityRpm);

    if (isMoving) {
        stopMotion("Drive was still running, stopped it");
    } else if (wasRunning) {
        stopMotion(wasLostWhileRunning ? "Drive back: it had halted itself, now switched off" : "Drive was enabled and holding, switched it off");
    } else {
        sdo.write(Objects::TARGET_VELOCITY, 0);
        sdo.write(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE);
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isLostWhileRunning = false;
    }

    ESP_LOGI(TAG,
             "Drive found: vendor 0x%" PRIx32 " product 0x%" PRIx32 " serial %" PRIu32 " \"%s\" hw \"%s\" sw \"%s\", node %" PRIu32 " bitrate code %" PRIu32 ", encoder %" PRIu32
             " counts/rev, 6085 %" PRIu32 " 605A %" PRId32 " 607F %" PRIu32 ", watchdog %" PRIu32 " ms, rated %" PRIu32 " cA, stator %" PRIu32 " mOhm",
             vendorId,
             productCode,
             serialNumber,
             current.deviceName,
             current.hardwareVersion,
             current.softwareVersion,
             nodeId,
             bitrateCode,
             increments,
             quickStopDeceleration,
             quickStopOptionCode,
             maxProfileVelocity,
             watchdogMs,
             ratedCurrentCentiamps,
             statorResistanceMilliohms);

    ESP_LOGI(TAG,
             "Position: 6064 %" PRId32 " units, %" PRId64 " units/turn (608F %" PRIu32 "/%" PRIu32 ", 6091 %" PRIu32 "/%" PRIu32 "), 607D %" PRId32 "..%" PRId32 ", 6065 %" PRIu32 ", 6067 %" PRIu32
             ", 60F2 %" PRIu32,
             positionUnits,
             unitsPerTurn,
             increments,
             encoderMotorRevolutions,
             gearMotorRevolutions,
             gearShaftRevolutions,
             positionLimitMin,
             positionLimitMax,
             followingErrorWindow,
             positionWindow,
             positioningOptionCode);

    // Which motor the log lines after this belong to
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        SessionLogEntry entry;
        entry.kind = SessionLogEntry::Kind::CONNECTED;
        entry.serialNumber = serialNumber;
        addSessionLogEntry(entry);
    }

    if (vendorId != EXPECTED_VENDOR_ID) {
        setMessage(MessageLevel::WARNING, "Drive found, but vendor 0x%" PRIx32 " is not NiMotion (0x509)", vendorId);
    } else if (!wasRunning) {
        setMessage(MessageLevel::INFO, wasLostWhileRunning ? "Drive back; it was not running" : "Drive found, ready");
    }

    // Lets the drive run on the battery pack (D-005); sets its own message if it fails
    lowerUndervoltageLimitIfNeeded();

    // A pulled cable then stops the motor (D-023); sets its own message if it fails
    enableHeartbeat();

    return true;
}

void MotorService::applyQuickStopLimit(uint32_t quickStopDecelerationCounts) {
    // No steeper than the quick stop, so STOP always brakes harder than a normal ramp (D-019)
    int64_t accelerationLimit = FALLBACK_ACCELERATION_LIMIT_RPM_PER_SECOND;

    if (quickStopDecelerationCounts > 0 && encoderIncrements > 0) {
        accelerationLimit = static_cast<int64_t>(quickStopDecelerationCounts) * 60 / encoderIncrements;
    }

    accelerationLimitRpmPerSecond = static_cast<int32_t>(std::max<int64_t>(accelerationLimit, MIN_ACCELERATION_RPM_PER_SECOND));

    // The current target follows a lower limit; the step test's ramp is capped when read
    setAccelerationRpmPerSecond(targetAccelerationRpmPerSecond);
}

void MotorService::updateLimits(uint32_t ratedSpeedRpm, uint32_t maxMotorSpeedRpm, uint32_t maxProfileVelocityCounts, uint32_t quickStopDecelerationCounts) {
    // Speed: the rated speed, capped by whichever drive maximum is lower (the console never runs past rated)
    int64_t limitRpm = ratedSpeedRpm > 0 ? ratedSpeedRpm : FALLBACK_VELOCITY_LIMIT_RPM;

    if (maxMotorSpeedRpm > 0) {
        limitRpm = std::min<int64_t>(limitRpm, maxMotorSpeedRpm);
    }

    if (maxProfileVelocityCounts > 0 && encoderIncrements > 0) {
        limitRpm = std::min<int64_t>(limitRpm, static_cast<int64_t>(maxProfileVelocityCounts) * 60 / encoderIncrements);
    }

    velocityLimitRpm = static_cast<int32_t>(std::max<int64_t>(limitRpm, 1));
    setTargetVelocityRpm(targetVelocityRpm);
    applyQuickStopLimit(quickStopDecelerationCounts);

    ESP_LOGI(TAG,
             "Limits: speed %" PRId32 " rpm (rated %" PRIu32 ", 6080 %" PRIu32 "), acceleration %" PRId32 " rpm/s",
             static_cast<int32_t>(velocityLimitRpm),
             ratedSpeedRpm,
             maxMotorSpeedRpm,
             static_cast<int32_t>(accelerationLimitRpmPerSecond));
}

void MotorService::readIdentityStrings() {
    MotorStatus strings;
    const ObjectEntry* entries[] = {&Objects::DEVICE_NAME, &Objects::HARDWARE_VERSION, &Objects::SOFTWARE_VERSION};
    char* buffers[] = {strings.deviceName, strings.hardwareVersion, strings.softwareVersion};
    size_t sizes[] = {sizeof(strings.deviceName), sizeof(strings.hardwareVersion), sizeof(strings.softwareVersion)};

    // Informative only: a failure leaves the string empty and is logged
    for (size_t index = 0; index < 3; index++) {
        SdoResult result = sdo.readString(*entries[index], buffers[index], sizes[index]);

        if (result != SdoResult::OK) {
            buffers[index][0] = '\0';
            ESP_LOGW(TAG, "Reading %04X failed: %s", entries[index]->index, SdoClient::getResultName(result));
        }

        keepAlive();
    }

    std::lock_guard<std::mutex> lock(statusMutex);
    snprintf(status.deviceName, sizeof(status.deviceName), "%s", strings.deviceName);
    snprintf(status.hardwareVersion, sizeof(status.hardwareVersion), "%s", strings.hardwareVersion);
    snprintf(status.softwareVersion, sizeof(status.softwareVersion), "%s", strings.softwareVersion);
}

void MotorService::enableHeartbeat() {
    uint32_t originalMs = 0;
    uint32_t faultAction = 0;
    sdo.read(Objects::HEARTBEAT_PRODUCER_MS, originalMs);
    sdo.read(Objects::COMMUNICATION_FAULT_ACTION, faultAction);

    // Run-time write, not saved: gone at the motor's power-off, written again at every connect
    uint32_t heartbeatMs = originalMs;

    if (writeObject(Objects::HEARTBEAT_PRODUCER_MS, HEARTBEAT_INTERVAL_MS, "Heartbeat")) {
        sdo.read(Objects::HEARTBEAT_PRODUCER_MS, heartbeatMs);
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.heartbeatMs = heartbeatMs;
        status.originalHeartbeatMs = originalMs;
        status.communicationFaultAction = faultAction;
    }

    // Response codes 4 (warning) and 5 (ignore) don't stop the motor (alarm table 9-1); 0 to 3 do
    uint32_t responseCode = (faultAction >> 24) & 0xFF;

    if (heartbeatMs > 0 && responseCode >= 4) {
        setMessage(MessageLevel::WARNING, "200E:09 = 0x%08" PRIX32 ": a pulled cable raises 0x7501 but won't stop the motor", faultAction);
    }

    ESP_LOGI(TAG, "Heartbeat %" PRIu32 " ms (drive had %" PRIu32 "), 0x7501 reaction 200E:09 = 0x%08" PRIX32, heartbeatMs, originalMs, faultAction);
}

void MotorService::dumpObjects() {
    MotorStatus current = getStatus();

    if (!current.isConnected || current.isRunning) {
        ESP_LOGW(TAG, "Dump refused: %s", current.isConnected ? "the motor is running" : "no drive");

        return;
    }

    // Index ranges to probe: CiA 301 communication objects, the manufacturer group, the CiA 402 group. Strings
    // (1008-100A) need segmented reads and are in the identity already
    struct IndexRange {
        uint16_t first;
        uint16_t last;
    };
    static constexpr IndexRange RANGES[] = {
        {0x1000, 0x1007}, {0x100B, 0x1030}, {0x1200, 0x1200}, {0x1400, 0x1403}, {0x1600, 0x1603}, {0x1800, 0x1803}, {0x1A00, 0x1A03}, {0x2000, 0x203F}, {0x6000, 0x60FF}};
    static constexpr uint32_t ABORT_NO_OBJECT = 0x06020000;
    static constexpr uint32_t ABORT_NO_SUB_INDEX = 0x06090011;
    static constexpr uint8_t MAX_SUB_INDEX = 0x40;
    static constexpr int MISSING_SUB_INDICES_TO_STOP = 4;

    ESP_LOGI(TAG, "DUMP start, serial %" PRIu32 ", firmware %s", current.serialNumber, current.softwareVersion);
    int64_t startUs = esp_timer_get_time();
    size_t objectCount = 0;

    for (const IndexRange& range : RANGES) {
        for (uint32_t index = range.first; index <= range.last; index++) {
            keepAlive();
            uint32_t abortCode = 0;

            // Sub-index 0: the value of a simple object, or the number of entries of an array or record
            if (!dumpObject(static_cast<uint16_t>(index), 0, abortCode)) {
                if (abortCode == ABORT_NO_OBJECT) {
                    continue;
                }
            }

            objectCount++;

            // Sub-indices from 1 until a run of missing ones; a simple object has none
            int missingInRow = 0;

            for (uint8_t subIndex = 1; subIndex <= MAX_SUB_INDEX && missingInRow < MISSING_SUB_INDICES_TO_STOP; subIndex++) {
                if (dumpObject(static_cast<uint16_t>(index), subIndex, abortCode)) {
                    missingInRow = 0;
                } else if (abortCode == ABORT_NO_SUB_INDEX || abortCode == ABORT_NO_OBJECT) {
                    missingInRow++;
                } else {
                    missingInRow = 0;
                }
            }
        }
    }

    ESP_LOGI(TAG, "DUMP done: %u objects in %" PRId64 " ms", static_cast<unsigned>(objectCount), (esp_timer_get_time() - startUs) / 1000);
}

bool MotorService::dumpObject(uint16_t index, uint8_t subIndex, uint32_t& abortCode) {
    uint32_t value = 0;
    SdoResult result = sdo.read(index, subIndex, value);
    abortCode = 0;

    if (result == SdoResult::OK) {
        ESP_LOGI(TAG, "DUMP %04X:%02X = 0x%08" PRIX32 " (%" PRIu32 ", %" PRId32 ")", index, subIndex, value, value, static_cast<int32_t>(value));

        return true;
    }

    if (result == SdoResult::ABORTED) {
        abortCode = sdo.lastAbortCode;

        // Missing objects and sub-indices are the probe's normal answer; anything else is worth seeing
        if (abortCode != 0x06020000 && abortCode != 0x06090011) {
            ESP_LOGI(TAG, "DUMP %04X:%02X aborted 0x%08" PRIX32 " (%s)", index, subIndex, abortCode, SdoClient::getAbortCodeDescription(abortCode));
        }

        return false;
    }

    ESP_LOGI(TAG, "DUMP %04X:%02X %s", index, subIndex, SdoClient::getResultName(result));

    return false;
}

/**
 * Returns the width of a table entry's type in bytes, or 0 for strings and unknown types.
 *
 * @param type The entry's type
 * @return Width in bytes
 */
static uint8_t getTypeWidthBytes(Parameters::Type type) {
    switch (type) {
        case Parameters::Type::UINT8:
        case Parameters::Type::INT8:
            return 1;

        case Parameters::Type::UINT16:
        case Parameters::Type::INT16:
            return 2;

        case Parameters::Type::UINT32:
        case Parameters::Type::INT32:
            return 4;

        default:
            return 0;
    }
}

/**
 * Returns the width of an object that reads all ones, from the pattern: the SDO client zero-extends, so a blank 8-bit
 * object reads 0xFF, a 16-bit one 0xFFFF.
 *
 * @param value The value as read
 * @return Width in bytes, or 0 when the value isn't all ones in any width
 */
static uint8_t getBlankWidthBytes(uint32_t value) {
    if (value == 0xFF) {
        return 1;
    }

    if (value == 0xFFFF) {
        return 2;
    }

    if (value == 0xFFFFFFFF) {
        return 4;
    }

    return 0;
}

/**
 * Returns the all-ones value of a width.
 *
 * @param widthBytes 1, 2 or 4
 * @return 0xFF, 0xFFFF or 0xFFFFFFFF
 */
static uint32_t getAllOnes(uint8_t widthBytes) {
    return widthBytes >= 4 ? 0xFFFFFFFF : (1u << (widthBytes * 8)) - 1;
}

/**
 * Tells whether the recovery may write a factory dump object (D-031): not a bus setting, save, restore or command object,
 * and when the parameter table knows it, read-write and not console-managed. Locked entries are allowed; they are blank
 * too. Objects the table doesn't know are allowed; the drive refuses the read-only ones.
 *
 * @param object The factory dump object
 * @param entry Its parameter table entry, or null when the table doesn't know it
 * @return True if the recovery writes it when it reads all ones
 */
static bool isRecoverableObject(const FactoryDump::Object& object, const Parameters::Entry* entry) {
    static constexpr uint16_t SKIPPED_INDEXES[] = {0x200C, 0x6040, 0x6060, 0x6071, 0x607A, 0x60C1, 0x60FF};
    static constexpr uint8_t SKIPPED_FLAGS = Parameters::Flag::SESSION | Parameters::Flag::CONSOLE_OWNED | Parameters::Flag::SETUP_ONLY;

    for (uint16_t index : SKIPPED_INDEXES) {
        if (object.index == index) {
            return false;
        }
    }

    if (entry == nullptr) {
        return true;
    }

    return entry->access == Parameters::Access::READ_WRITE && (entry->flags & SKIPPED_FLAGS) == 0 && getTypeWidthBytes(entry->type) != 0;
}

void MotorService::recoverBlankFlash() {
    static constexpr uint32_t ABORT_READ_ONLY = 0x06010002;

    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    if (!current.isConnected || current.isRunning || isPowered) {
        setMessage(MessageLevel::WARNING, "Recovery refused: %s", current.isConnected ? "switch the motor off first" : "no drive");

        return;
    }

    if (!current.runChecks.isParameterFlashBlank) {
        setMessage(MessageLevel::WARNING, "Recovery refused: the parameter flash isn't blank");

        return;
    }

    // Step 1: the factory dump's value into every object that reads all ones (the manual procedure of 2026-10-15). The
    // undocumented objects matter too: 2000:20 kept 0x7308 alive on motor 159709 after the documented ones were back
    setMessage(MessageLevel::INFO, "Recovering: writing factory values to every blank object");
    uint16_t writeCount = 0;
    uint16_t refusedCount = 0;

    for (size_t position = 0; position < FactoryDump::OBJECT_COUNT; position++) {
        const FactoryDump::Object& object = FactoryDump::OBJECTS[position];
        const Parameters::Entry* entry = Parameters::find(object.index, object.subIndex);

        if (!isRecoverableObject(object, entry)) {
            continue;
        }

        if ((position % 8) == 0) {
            keepAlive();
        }

        uint32_t value = 0;

        if (sdo.read(object.index, object.subIndex, value) != SdoResult::OK) {
            continue;
        }

        // The table's type gives the width; an unknown object gives it away by its blank pattern
        uint8_t widthBytes = entry != nullptr ? getTypeWidthBytes(entry->type) : getBlankWidthBytes(value);

        if (widthBytes == 0) {
            continue;
        }

        uint32_t allOnes = getAllOnes(widthBytes);

        if ((value & allOnes) != allOnes) {
            continue;
        }

        uint32_t factory = object.value & allOnes;

        // A factory value of all ones (2002:1D = -1, the PDO transmission types) is already there
        if (factory == allOnes) {
            continue;
        }

        SdoResult result = sdo.write(object.index, object.subIndex, factory, widthBytes);

        if (result == SdoResult::OK) {
            writeCount++;
            ESP_LOGI(TAG, "Recovery: %04X:%02X <- 0x%08" PRIX32 " (%s)", object.index, object.subIndex, factory, entry != nullptr ? entry->name : "undocumented");
        } else if (result == SdoResult::ABORTED && sdo.lastAbortCode == ABORT_READ_ONLY) {
            // Status and measurement objects read all ones too; not ours to write
            ESP_LOGD(TAG, "Recovery: %04X:%02X is read-only", object.index, object.subIndex);
        } else {
            refusedCount++;
            ESP_LOGW(TAG,
                     "Recovery: %04X:%02X <- 0x%08" PRIX32 " %s 0x%08" PRIX32,
                     object.index,
                     object.subIndex,
                     factory,
                     SdoClient::getResultName(result),
                     result == SdoResult::ABORTED ? sdo.lastAbortCode : 0);
        }
    }

    ESP_LOGI(TAG, "Recovery: %u objects written, %u refused", writeCount, refusedCount);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        // The dump's values are a BLM42's: foreign until the restore replaces them (D-031)
        status.isRecoveredNotRestored = writeCount > 0;
        SessionLogEntry entry;
        entry.kind = SessionLogEntry::Kind::RECOVERED;
        entry.writeCount = writeCount;
        entry.refusedCount = refusedCount;
        addSessionLogEntry(entry);
    }

    // Step 2: the fault reset, as RESET FAULT does; with valid parameters 0x6320 and 0x7308 clear on their own
    resetFault();
    current = getStatus();

    if (current.state == DriveState::FAULT || current.state == DriveState::FAULT_REACTION_ACTIVE) {
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            SessionLogEntry entry;
            entry.kind = SessionLogEntry::Kind::RECOVER_FAILED;
            entry.resetBy = "fault reset";
            addSessionLogEntry(entry);
        }

        setMessage(MessageLevel::ERROR, "Recovery stopped: the fault didn't clear (0x%04X) after %u objects were rewritten", current.errorCode, writeCount);

        return;
    }

    // Step 3: the drive's own factory values into its flash; the restore says why if the drive refuses
    if (!restoreDefaults()) {
        std::lock_guard<std::mutex> lock(statusMutex);
        SessionLogEntry entry;
        entry.kind = SessionLogEntry::Kind::RECOVER_FAILED;
        entry.resetBy = "restore";
        addSessionLogEntry(entry);

        return;
    }

    // Until it boots again the drive may be inconsistent (motor 159709 ran away after its recovery); motion waits
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.needsPowerCycle = writeCount > 0;
    }

    setMessage(MessageLevel::INFO, "Recovered: %u objects rewritten, defaults restored. Power-cycle the motor before running it, so everything loads from the restored values", writeCount);
}

void MotorService::blankParametersForTest() {
    static constexpr uint32_t ABORT_READ_ONLY = 0x06010002;
    static constexpr uint32_t ABORT_CLASS_MASK = 0xFFFF0000;
    static constexpr uint32_t ABORT_LENGTH_CLASS = 0x06070000; // 0x0607xxxx: the data type or length doesn't match
    static constexpr uint8_t WIDTH_GUESSES[] = {2, 4, 1};

    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    if (!current.isConnected || current.isRunning || isPowered) {
        ESP_LOGW(TAG, "Blank refused: %s", current.isConnected ? "switch the motor off first" : "no drive");

        return;
    }

    // The same set the recovery writes, so the test covers exactly its path
    uint16_t writeCount = 0;
    uint16_t refusedCount = 0;

    for (size_t position = 0; position < FactoryDump::OBJECT_COUNT; position++) {
        const FactoryDump::Object& object = FactoryDump::OBJECTS[position];
        const Parameters::Entry* entry = Parameters::find(object.index, object.subIndex);

        if (!isRecoverableObject(object, entry)) {
            continue;
        }

        if ((position % 8) == 0) {
            keepAlive();
        }

        // An unknown object's width is a guess from its value; a length abort means try the other widths
        uint8_t widthBytes = entry != nullptr ? getTypeWidthBytes(entry->type) : (object.value > 0xFFFF ? 4 : 2);
        size_t guess = 0;
        SdoResult result = SdoResult::OK;

        while (true) {
            result = sdo.write(object.index, object.subIndex, getAllOnes(widthBytes), widthBytes);
            bool isLengthAbort = result == SdoResult::ABORTED && (sdo.lastAbortCode & ABORT_CLASS_MASK) == ABORT_LENGTH_CLASS;

            if (!isLengthAbort || entry != nullptr || guess >= sizeof(WIDTH_GUESSES)) {
                break;
            }

            widthBytes = WIDTH_GUESSES[guess++];
        }

        if (result == SdoResult::OK) {
            writeCount++;
        } else if (result == SdoResult::ABORTED && sdo.lastAbortCode == ABORT_READ_ONLY) {
            // Not writable, so never blank by this test either
        } else {
            refusedCount++;
            ESP_LOGI(TAG,
                     "Blank: %04X:%02X (%s) refused, %s 0x%08" PRIX32,
                     object.index,
                     object.subIndex,
                     entry != nullptr ? entry->name : "undocumented",
                     SdoClient::getResultName(result),
                     result == SdoResult::ABORTED ? sdo.lastAbortCode : 0);
        }
    }

    ESP_LOGW(TAG, "Blank for test: %u objects set to all ones, %u refused. Nothing is saved; RECOVER or a power cycle puts the values back", writeCount, refusedCount);

    // Detection as at connect; the Faults screen follows the flag
    readRunCheckValues();
    isFaultRecordStale = true;
    setMessage(MessageLevel::WARNING, "Test: %u parameters set to all ones at run time (%u refused)", writeCount, refusedCount);
}

// The constants the identification writes, in CalibrationStatus::Constant order
static const ObjectEntry* const CALIBRATION_OBJECTS[CalibrationStatus::CONSTANT_COUNT] = {
    &Objects::INITIAL_ANGLE,
    &Objects::STATOR_RESISTANCE,
    &Objects::STATOR_INDUCTANCE_LQ,
    &Objects::STATOR_INDUCTANCE_LD,
    &Objects::TORQUE_COEFFICIENT,
    &Objects::ROTOR_INERTIA,
};

static const char* const CALIBRATION_NAMES[CalibrationStatus::CONSTANT_COUNT] = {"initial angle", "stator resistance", "inductance Lq", "inductance Ld", "torque coefficient", "rotor inertia"};

/**
 * Returns the angle's change in electrical degrees, 65536 per turn, wrapped to -180..180 (inferred unit, bench
 * 2026-10-15: successive identifications differed by a consistent 80-100 degrees on a slipping motor and 1-6 on a
 * healthy one).
 *
 * @param before The angle as the drive had it
 * @param after The angle as identified
 * @return Degrees
 */
static int32_t getAngleChangeDegrees(uint32_t before, uint32_t after) {
    int16_t difference = static_cast<int16_t>(static_cast<uint16_t>(after) - static_cast<uint16_t>(before));

    return static_cast<int32_t>(difference) * 360 / 65536;
}

void MotorService::publishCalibration(const CalibrationStatus& calibration) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status.calibration = calibration;
    status.calibrationVersion++;
}

void MotorService::runCalibration() {
    static constexpr uint32_t CALIBRATION_TIMEOUT_MS = 60000; // A run took 15-30 s on the bench
    static constexpr uint32_t CALIBRATION_POLL_MS = 100;
    static constexpr uint16_t RUN_FAULT_CODES[] = {0xFF01, 0xFF0C, 0x7310, 0x2300, 0x2312}; // Identification failure, calibration timeout, overspeed, overcurrent, stall

    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    if (!current.isConnected || current.isRunning || isPowered) {
        setMessage(MessageLevel::WARNING, "Calibration refused: %s", current.isConnected ? "switch the motor off first" : "no drive");

        return;
    }

    if (current.runChecks.isParameterFlashBlank) {
        setMessage(MessageLevel::WARNING, "Calibration refused: the parameter flash is blank, use RECOVER first");

        return;
    }

    if (current.needsPowerCycle) {
        setMessage(MessageLevel::WARNING, "Calibration refused: power-cycle the motor first");

        return;
    }

    if (current.isBelowPackCutoff) {
        setMessage(MessageLevel::WARNING, "Calibration refused: pack below the cutoff");

        return;
    }

    uint32_t virtualInputsEnabled = 0;

    if (sdo.read(Objects::VIRTUAL_INPUTS_ENABLED, virtualInputsEnabled) != SdoResult::OK || virtualInputsEnabled != 1) {
        setMessage(MessageLevel::WARNING, "Calibration refused: the virtual inputs are off (200C:07 = %" PRIu32 ")", virtualInputsEnabled);

        return;
    }

    // What the drive has now: the constants it will overwrite, and the settings the run borrows
    CalibrationStatus calibration;

    for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
        if (sdo.read(*CALIBRATION_OBJECTS[constant], calibration.before[constant]) != SdoResult::OK) {
            setMessage(MessageLevel::WARNING, "Calibration refused: couldn't read %04X:%02X", CALIBRATION_OBJECTS[constant]->index, CALIBRATION_OBJECTS[constant]->subIndex);

            return;
        }
    }

    uint32_t ownVdiFunction = 0;
    uint32_t ownVdiLogic = 0;
    sdo.read(Objects::VDI1_FUNCTION, ownVdiFunction);
    sdo.read(Objects::VDI1_LOGIC, ownVdiLogic);

    int64_t startUs = esp_timer_get_time();
    calibration.state = CalibrationStatus::State::RUNNING;
    publishCalibration(calibration);
    setMessage(MessageLevel::INFO, "Calibrating: the shaft locks, then spins both ways. STOP ends it");
    ESP_LOGI(TAG,
             "Calibration: before angle %" PRIu32 ", R %" PRIu32 ", Lq %" PRIu32 ", Ld %" PRIu32 ", Kt %" PRIu32 ", J %" PRIu32,
             calibration.before[0],
             calibration.before[1],
             calibration.before[2],
             calibration.before[3],
             calibration.before[4],
             calibration.before[5]);

    // NiMotion's sequence (recalibration kit, 2023; bench 2026-10-15): VDI1 as servo enable, angle to 0 so the lock
    // stage measures it, mode 5, then the virtual enable
    bool isStarted = writeObject(Objects::VDI1_FUNCTION, INPUT_FUNCTION_SERVO_ENABLE, "Calibration: VDI1 function") && writeObject(Objects::VDI1_LOGIC, 0, "Calibration: VDI1 logic") &&
                     writeObject(Objects::INITIAL_ANGLE, 0, "Calibration: angle to 0") && writeObject(Objects::CONTROL_MODE, ControlMode::IDENTIFICATION, "Calibration: control mode 5") &&
                     writeObject(Objects::VIRTUAL_INPUT_LEVELS, 1, "Calibration: enable");

    if (!isStarted) {
        calibration.state = CalibrationStatus::State::FAILED;
        snprintf(calibration.reason, sizeof(calibration.reason), "a write failed");
        endCalibration(calibration, ControlMode::CIA402, ownVdiFunction, ownVdiLogic);

        return;
    }

    // The drive runs the stages on its own: 1 locked shaft, 2 spinning, 3 done. Then it leaves Operation enabled by
    // itself (V3.2, bench 2026-10-15) or stays enabled with the shaft still (V0.2.5.a5, bench 2026-10-16)
    static constexpr uint32_t DONE_STILL_MS = 1000;
    bool wasEnabled = false;
    uint32_t lastStage = 0xFFFF;
    int64_t stillSinceUs = 0;

    while (true) {
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(CALIBRATION_POLL_MS));

        if (isStopRequested) {
            calibration.state = CalibrationStatus::State::CANCELLED;
            snprintf(calibration.reason, sizeof(calibration.reason), "stopped");
            break;
        }

        uint32_t stage = 0;
        uint32_t statusword = 0;
        uint32_t errorCode = 0;
        int32_t speedRpm = 0;
        bool isRead = sdo.read(Objects::IDENTIFICATION_STAGE, stage) == SdoResult::OK && sdo.read(Objects::STATUSWORD, statusword) == SdoResult::OK &&
                      sdo.read(Objects::ERROR_CODE, errorCode) == SdoResult::OK && sdo.readSigned(Objects::VELOCITY_ACTUAL, speedRpm) == SdoResult::OK;

        if (!isRead) {
            calibration.state = CalibrationStatus::State::FAILED;
            snprintf(calibration.reason, sizeof(calibration.reason), "the drive stopped answering");
            break;
        }

        DriveState state = decodeDriveState(static_cast<uint16_t>(statusword));
        calibration.stage = static_cast<uint8_t>(stage);
        calibration.speedRpm = speedRpm;
        calibration.elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - startUs) / 1000);
        publishCalibration(calibration);

        if (state == DriveState::OPERATION_ENABLED) {
            wasEnabled = true;
        }

        if (stage != lastStage) {
            ESP_LOGI(TAG, "Calibration: stage %" PRIu32 " at %" PRIu32 " ms, %" PRId32 " rpm, statusword 0x%04" PRIX32, stage, calibration.elapsedMs, speedRpm, statusword);
            lastStage = stage;
        }

        bool isRunFault = state == DriveState::FAULT || state == DriveState::FAULT_REACTION_ACTIVE;

        for (uint16_t code : RUN_FAULT_CODES) {
            if (errorCode == code) {
                isRunFault = true;
            }
        }

        if (isRunFault) {
            calibration.state = CalibrationStatus::State::FAILED;
            calibration.faultCode = static_cast<uint16_t>(errorCode);
            const AlarmCode* alarm = AlarmCodes::find(static_cast<uint16_t>(errorCode));
            snprintf(calibration.reason, sizeof(calibration.reason), "%s (0x%04" PRIX32 ") in stage %" PRIu32, alarm != nullptr ? alarm->name : "alarm", errorCode, stage);
            break;
        }

        // Done: stage 3 and either the drive switched itself off, or the shaft has been still for a second
        if (stage >= 3 && wasEnabled) {
            bool isStill = std::abs(speedRpm) < STOPPED_THRESHOLD_RPM;

            if (!isStill) {
                stillSinceUs = 0;
            } else if (stillSinceUs == 0) {
                stillSinceUs = esp_timer_get_time();
            }

            bool wasStillLongEnough = stillSinceUs != 0 && esp_timer_get_time() - stillSinceUs > static_cast<int64_t>(DONE_STILL_MS) * 1000;

            if (state != DriveState::OPERATION_ENABLED || wasStillLongEnough) {
                calibration.state = CalibrationStatus::State::DONE;
                break;
            }
        }

        if (calibration.elapsedMs > CALIBRATION_TIMEOUT_MS) {
            calibration.state = CalibrationStatus::State::FAILED;
            snprintf(calibration.reason, sizeof(calibration.reason), "not finished after %" PRIu32 " s", CALIBRATION_TIMEOUT_MS / 1000);
            break;
        }
    }

    calibration.elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - startUs) / 1000);
    endCalibration(calibration, ControlMode::CIA402, ownVdiFunction, ownVdiLogic);
}

void MotorService::endCalibration(CalibrationStatus& calibration, uint32_t ownControlMode, uint32_t ownVdiFunction, uint32_t ownVdiLogic) {
    static constexpr uint32_t COAST_CHECK_MS = 500;
    static constexpr uint32_t COAST_SETTLE_MS = 300;

    // In mode 5 the virtual input is the enable: off first, whatever happened
    writeObject(Objects::VIRTUAL_INPUT_LEVELS, 0, "Calibration: disable");

    if (calibration.state == CalibrationStatus::State::CANCELLED) {
        // The spin stage may still be going: the ordinary stop, and coast if the ramp doesn't bite (D2 of the plan; the
        // quick-stop ramp couldn't stop a runaway on 2026-10-15, coast did)
        int32_t speedAtStop = 0;
        sdo.readSigned(Objects::VELOCITY_ACTUAL, speedAtStop);
        stopMotion("Calibration stopped");

        for (uint32_t waitedMs = 0; waitedMs < COAST_CHECK_MS; waitedMs += SAMPLE_INTERVAL_MS) {
            keepAlive();
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        }

        int32_t speedNow = 0;
        sdo.readSigned(Objects::VELOCITY_ACTUAL, speedNow);

        if (std::abs(speedNow) > STOPPED_THRESHOLD_RPM && std::abs(speedNow) > std::abs(speedAtStop) * 8 / 10) {
            uint32_t ownQuickStopOption = static_cast<uint32_t>(getStatus().quickStopOptionCode);
            ESP_LOGW(TAG, "Calibration stop: %" PRId32 " rpm after the ramp, coasting instead", speedNow);
            writeObject(Objects::QUICK_STOP_OPTION_CODE, 0, "Calibration: coast");
            writeObject(Objects::CONTROLWORD, Controlword::QUICK_STOP, "Calibration: quick stop");
            vTaskDelay(pdMS_TO_TICKS(COAST_SETTLE_MS));
            writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Calibration: power stage off");
            writeObject(Objects::QUICK_STOP_OPTION_CODE, ownQuickStopOption, "Calibration: quick-stop option back");
        }

        // Consumed here; the task loop's own stop would only repeat it
        isStopRequested.exchange(false);
    }

    // The identified values, or the drive's own ones back when the run didn't finish
    if (calibration.state == CalibrationStatus::State::DONE) {
        for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
            sdo.read(*CALIBRATION_OBJECTS[constant], calibration.after[constant]);
        }
    } else {
        for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
            writeObject(*CALIBRATION_OBJECTS[constant], calibration.before[constant], "Calibration: value back");
        }
    }

    // CiA 402 mode again, and the virtual input as the motor had it
    writeObject(Objects::CONTROL_MODE, ownControlMode, "Calibration: control mode back");
    writeObject(Objects::VDI1_FUNCTION, ownVdiFunction, "Calibration: VDI1 function back");
    writeObject(Objects::VDI1_LOGIC, ownVdiLogic, "Calibration: VDI1 logic back");

    // A failed run leaves the drive in Fault: reset it so the motor is usable again, switched off
    poll();

    if (getStatus().state == DriveState::FAULT) {
        sendFaultReset();
        writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Calibration: power stage off");
        poll();
    }

    isFaultRecordStale = true;

    // The drive ran in another mode with its constants rewritten at run time: it boots before it moves again (D-031,
    // D-032); KEEP or DISCARD have to happen before, since the identified values are in RAM
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.needsPowerCycle = true;
    }

    publishCalibration(calibration);

    switch (calibration.state) {
        case CalibrationStatus::State::DONE: {
            // The identified values are unsaved changes like any edit (D-028, D-032): SAVE keeps them, REVERT writes the
            // stored values back. The entries are locked in the UI; the drive wrote them, not the operator
            {
                std::lock_guard<std::mutex> lock(statusMutex);

                for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
                    const Parameters::Entry* entry = Parameters::find(CALIBRATION_OBJECTS[constant]->index, CALIBRATION_OBJECTS[constant]->subIndex);

                    if (entry != nullptr) {
                        recordUnsavedChange(static_cast<size_t>(entry - Parameters::ENTRIES), calibration.before[constant], calibration.after[constant]);
                    }
                }
            }

            int32_t angleChangeDegrees = getAngleChangeDegrees(calibration.before[CalibrationStatus::INITIAL_ANGLE], calibration.after[CalibrationStatus::INITIAL_ANGLE]);
            setMessage(MessageLevel::INFO,
                       "Calibrated in %" PRIu32 " s: angle %" PRIu32 " -> %" PRIu32 " (%+" PRId32 "°). SAVE keeps it, REVERT puts the stored values back; then power-cycle the motor",
                       calibration.elapsedMs / 1000,
                       calibration.before[CalibrationStatus::INITIAL_ANGLE],
                       calibration.after[CalibrationStatus::INITIAL_ANGLE],
                       angleChangeDegrees);

            for (size_t constant = 0; constant < CalibrationStatus::CONSTANT_COUNT; constant++) {
                ESP_LOGI(TAG, "Calibration: %s %" PRIu32 " -> %" PRIu32, CALIBRATION_NAMES[constant], calibration.before[constant], calibration.after[constant]);
            }

            break;
        }

        case CalibrationStatus::State::FAILED:
            setMessage(MessageLevel::ERROR, "Calibration failed: %s. The stored values are back; power-cycle the motor before running it", calibration.reason);
            break;

        case CalibrationStatus::State::CANCELLED:
            setMessage(MessageLevel::WARNING, "Calibration stopped: the stored values are back. Power-cycle the motor before running it");
            break;

        default:
            break;
    }

    ESP_LOGI(TAG, "Calibration %s after %" PRIu32 " ms: %s", calibration.state == CalibrationStatus::State::DONE ? "done" : "ended", calibration.elapsedMs, calibration.reason);
}

void MotorService::reportFlashRefusal(const char* action, const char* statusObject, uint32_t statusValue, int32_t busVoltageDeciV) {
    MotorStatus current = getStatus();
    uint32_t ratedVoltageV = 0;
    bool isBelowGuard = sdo.read(Objects::RATED_VOLTAGE, ratedVoltageV) == SdoResult::OK && busVoltageDeciV < static_cast<int32_t>(ratedVoltageV) * FLASH_WRITE_MIN_PERCENT / 10;

    if (current.runChecks.isParameterFlashBlank && !current.isRecoveredNotRestored) {
        setMessage(MessageLevel::ERROR, "The drive refused the %s (%s = %" PRIu32 "): its parameter flash is blank. Use RECOVER on the Faults screen", action, statusObject, statusValue);
    } else if (isBelowGuard) {
        setMessage(MessageLevel::ERROR,
                   "The drive refused the %s: its supply is too low for flash writes (bus %" PRId32 ".%" PRId32 " V, needs %" PRIu32 " V)",
                   action,
                   busVoltageDeciV / 10,
                   busVoltageDeciV % 10,
                   ratedVoltageV * FLASH_WRITE_MIN_PERCENT / 100);
    } else {
        setMessage(MessageLevel::ERROR, "The drive refused the %s (%s = %" PRIu32 ", 603F 0x%04X): nothing changed", action, statusObject, statusValue, current.errorCode);
    }
}

void MotorService::performRawSdo() {
    RawSdoRequest request;

    {
        std::lock_guard<std::mutex> lock(rawSdoMutex);
        request = rawSdoRequest;
    }

    if (!getStatus().isConnected) {
        ESP_LOGW(TAG, "SDO %04X:%02X: no drive", request.index, request.subIndex);

        return;
    }

    SdoResult result = SdoResult::OK;
    uint32_t value = 0;
    char text[64] = "";

    switch (request.kind) {
        case RawSdoRequest::Kind::READ:
            result = sdo.read(request.index, request.subIndex, value);
            break;

        case RawSdoRequest::Kind::READ_STRING:
            result = sdo.readString(request.index, request.subIndex, text, sizeof(text));
            break;

        case RawSdoRequest::Kind::WRITE:
            result = sdo.write(request.index, request.subIndex, request.value, request.sizeBytes);
            break;

        case RawSdoRequest::Kind::NMT:
            result = sdo.sendNmt(static_cast<uint8_t>(request.value)) ? SdoResult::OK : SdoResult::SEND_FAILED;
            break;
    }

    // A write may change what the Faults screen and the parameter list show
    isFaultRecordStale = true;

    if (result == SdoResult::ABORTED) {
        ESP_LOGW(TAG, "SDO %04X:%02X aborted 0x%08" PRIX32 " (%s)", request.index, request.subIndex, sdo.lastAbortCode, SdoClient::getAbortCodeDescription(sdo.lastAbortCode));

        return;
    }

    if (result != SdoResult::OK) {
        ESP_LOGW(TAG, "SDO %04X:%02X %s", request.index, request.subIndex, SdoClient::getResultName(result));

        return;
    }

    switch (request.kind) {
        case RawSdoRequest::Kind::READ:
            ESP_LOGI(TAG, "SDO %04X:%02X = 0x%08" PRIX32 " (%" PRIu32 ", %" PRId32 ")", request.index, request.subIndex, value, value, static_cast<int32_t>(value));
            break;

        case RawSdoRequest::Kind::READ_STRING:
            ESP_LOGI(TAG, "SDO %04X:%02X = \"%s\"", request.index, request.subIndex, text);
            break;

        case RawSdoRequest::Kind::WRITE:
            ESP_LOGI(TAG, "SDO %04X:%02X <- 0x%08" PRIX32 " (%u bytes) OK", request.index, request.subIndex, request.value, static_cast<unsigned>(request.sizeBytes));
            break;

        case RawSdoRequest::Kind::NMT:
            ESP_LOGI(TAG, "NMT 0x%02X sent to node %u", static_cast<unsigned>(request.value), static_cast<unsigned>(sdo.getNodeId()));
            break;
    }
}

bool MotorService::sendFaultReset() {
    if (!writeObject(Objects::CONTROLWORD, Controlword::FAULT_RESET, "Fault reset")) {
        return false;
    }

    // The reset is bit 7's rising edge; cleared at once it was sometimes missed (failed motor, bench 2026-10-11), so it
    // stays set long enough for the drive to see it
    for (uint32_t heldMs = 0; heldMs < FAULT_RESET_HOLD_MS; heldMs += SAMPLE_INTERVAL_MS) {
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    return true;
}

void MotorService::resetFault() {
    MotorStatus before = getStatus();

    if (!sendFaultReset() || !writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Fault reset")) {
        return;
    }

    // The reset leaves the drive disabled, so a run (a halted one, after a stall) is over: START begins a new one
    appliedVelocityRpm = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = false;
        SessionLogEntry entry;
        entry.kind = SessionLogEntry::Kind::RESET;
        entry.code = before.errorCode;
        entry.resetBy = "RESET FAULT";
        addSessionLogEntry(entry);
    }

    // Give the drive time to act on it, then see what's left
    for (uint32_t waitedMs = 0; waitedMs < FAULT_RESET_SETTLE_MS; waitedMs += SAMPLE_INTERVAL_MS) {
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    poll();
    isFaultRecordStale = true;
    MotorStatus after = getStatus();
    bool isStillFaulted = (after.statusword & StatuswordBit::FAULT) != 0;
    const AlarmCode* alarm = AlarmCodes::find(after.errorCode);
    const char* name = alarm != nullptr ? alarm->shortName : "faulted";

    // The cause decides what to say: a reading for voltage and temperature, a power cycle for the hardware alarms
    if (!isStillFaulted) {
        setMessage(MessageLevel::INFO, "Fault cleared");
    } else if (after.errorCode == AlarmCodes::UNDERVOLTAGE) {
        setMessage(MessageLevel::WARNING,
                   "Still %s: bus %" PRId32 ".%" PRId32 " V, the drive needs more than %" PRIu32 " V",
                   name,
                   after.busVoltageDeciV / 10,
                   after.busVoltageDeciV % 10,
                   after.undervoltageLimitV);
    } else if (after.errorCode == AlarmCodes::OVERTEMPERATURE || after.errorCode == AlarmCodes::UNDERTEMPERATURE) {
        setMessage(MessageLevel::WARNING, "Still %s: drive at %" PRId32 " °C", name, after.temperatureC);
    } else if (AlarmCodes::needsPowerCycle(after.errorCode)) {
        setMessage(MessageLevel::WARNING, "Still %s: this alarm needs a motor power cycle", name);
    } else {
        setMessage(MessageLevel::WARNING, "Still %s (0x%04X): the cause is still there", name, after.errorCode);
    }
}

size_t MotorService::getSessionLog(SessionLogEntry* entries, size_t maxCount) {
    std::lock_guard<std::mutex> lock(statusMutex);
    size_t available = std::min<size_t>(sessionLogCount, SESSION_LOG_LENGTH);
    size_t count = std::min(available, maxCount);

    // Newest first: the entry before the next write position, going back
    for (size_t index = 0; index < count; index++) {
        entries[index] = sessionLog[(sessionLogCount - 1 - index) % SESSION_LOG_LENGTH];
    }

    return count;
}

void MotorService::addSessionLogEntry(SessionLogEntry entry) {
    static constexpr uint32_t MERGE_WINDOW_MS = 1000;

    entry.timeMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    entry.mode = status.isRunning ? status.motionMode : MotionMode::NONE;

    // The motion just before: the newest sample
    if (status.motionSampleCount > 0) {
        const MotionSample& sample = motionHistory[(status.motionSampleCount - 1) % MOTION_HISTORY_LENGTH];
        entry.velocityRpm = sample.velocityRpm;
        entry.currentCentiamps = sample.currentCentiamps;
        entry.positionMilliturns = sample.positionMilliturns;
        entry.hasMotion = status.isConnected;
    }

    // A flapping alarm (undervoltage at the edge) becomes one line with a count
    if (sessionLogCount > 0) {
        SessionLogEntry& newest = sessionLog[(sessionLogCount - 1) % SESSION_LOG_LENGTH];
        bool isSame = newest.kind == entry.kind && newest.code == entry.code && newest.serialNumber == entry.serialNumber;

        if (isSame && entry.timeMs - newest.timeMs < MERGE_WINDOW_MS) {
            newest.count++;
            newest.timeMs = entry.timeMs;
            status.sessionLogVersion++;

            return;
        }
    }

    sessionLog[sessionLogCount % SESSION_LOG_LENGTH] = entry;
    sessionLogCount++;
    status.sessionLogVersion++;
}

bool MotorService::putBackOwnValues(const SavePreview& preview) {
    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[position];
        bool isConsoleWritten = (entry.flags & (Parameters::Flag::SESSION | Parameters::Flag::CONSOLE_OWNED)) != 0;

        if (!isConsoleWritten || entry.access != Parameters::Access::READ_WRITE) {
            continue;
        }

        // The undervoltage limit stays at the battery value when the bus can't take the motor's own
        bool isUndervoltageLimit = entry.index == Objects::BUS_UNDERVOLTAGE_LIMIT_V.index && entry.subIndex == Objects::BUS_UNDERVOLTAGE_LIMIT_V.subIndex;

        if (isUndervoltageLimit && preview.keepsBatteryUndervoltage) {
            continue;
        }

        ParameterValue value;

        {
            std::lock_guard<std::mutex> lock(statusMutex);
            value = parameterValues[position];
        }

        uint32_t own = 0;

        if (preview.usesFactoryForOwn && entry.hasFactory) {
            own = static_cast<uint32_t>(entry.factory);
        } else if (value.hasOwnValue) {
            own = value.ownRaw;
        } else {
            continue;
        }

        SdoResult result = sdo.write(entry.index, entry.subIndex, own, ParameterFormat::getSizeBytes(entry));

        if (result != SdoResult::OK) {
            setMessage(MessageLevel::ERROR,
                       "Save stopped: putting back %s failed (%s)",
                       entry.description[0] != '\0' ? entry.description : entry.name,
                       result == SdoResult::ABORTED ? SdoClient::getAbortCodeDescription(sdo.lastAbortCode) : SdoClient::getResultName(result));

            return false;
        }

        ESP_LOGI(TAG, "Save: %04X:%02X back to 0x%08" PRIX32 "%s", entry.index, entry.subIndex, own, preview.usesFactoryForOwn ? " (factory)" : "");
        keepAlive();
    }

    return true;
}

void MotorService::saveParameters() {
    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    // Never under a running or holding motor: the console's motion values go back to the motor's own first
    if (!current.isConnected || current.isRunning || isPowered) {
        setMessage(MessageLevel::WARNING, "Save refused: %s", current.isConnected ? "switch the motor off first" : "no drive");

        return;
    }

    // After a recovery the drive holds the console's table values, another model's perhaps, until a restore (D-031)
    if (current.isRecoveredNotRestored) {
        setMessage(MessageLevel::WARNING, "Save refused: the drive holds the console's values, not its own. Restore defaults first");

        return;
    }

    SavePreview preview = getSavePreview();
    setMessage(MessageLevel::INFO, "Saving");

    // The console's own values back (D-028), so they aren't saved into a motor that goes back into a PTU
    if (!putBackOwnValues(preview)) {
        lowerUndervoltageLimitIfNeeded();
        enableHeartbeat();

        return;
    }

    // On a low bus the drive refuses flash writes unless 2000:07 is lowered; then that value is saved too (D-028)
    if (preview.lowersRatedVoltage) {
        writeObject(Objects::RATED_VOLTAGE, preview.loweredRatedVoltageV, "Rated voltage for the save");
        ESP_LOGI(
            TAG, "Rated voltage 2000:07 lowered from %" PRIu32 " V to %" PRIu32 " V for the save (bus %" PRId32 " dV)", preview.ratedVoltageV, preview.loweredRatedVoltageV, current.busVoltageDeciV);
    }

    // The save itself: 1010:01 with "save", nothing else (safety rules)
    SdoResult result = sdo.write(Objects::SAVE_PARAMETERS, SAVE_COMMAND_SAVE);
    lastFlashWriteUs = esp_timer_get_time();

    if (result != SdoResult::OK) {
        // An abort means the drive refused; no answer means the outcome is unknown (the cable, or the drive's flash pause)
        if (result == SdoResult::ABORTED) {
            setMessage(MessageLevel::ERROR, "Save refused: %s", SdoClient::getAbortCodeDescription(sdo.lastAbortCode));
        } else {
            setMessage(MessageLevel::ERROR, "Save outcome unknown: the drive didn't answer (%s). Read the parameters again to check", SdoClient::getResultName(result));
        }

        lowerUndervoltageLimitIfNeeded();
        enableHeartbeat();

        return;
    }

    // Accepted isn't done: a drive that can't write its flash raises a save fault (as with restore, D-025)
    for (uint32_t waitedMs = 0; waitedMs < RESTORE_CHECK_DELAY_MS; waitedMs += SAMPLE_INTERVAL_MS) {
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    uint32_t errorCode = 0;
    bool isRefused = sdo.read(Objects::ERROR_CODE, errorCode) == SdoResult::OK && errorCode == AlarmCodes::SAVE_FAULT;

    // The drive's own verdict: 1010:01 reads 1 after a save it did, anything else after one it refused (manual §7.6.1)
    uint32_t saveStatus = 1;

    if (sdo.read(Objects::SAVE_PARAMETERS, saveStatus) == SdoResult::OK && saveStatus != 1) {
        isRefused = true;
    }

    // The console's session settings again, as at connect
    lowerUndervoltageLimitIfNeeded();
    enableHeartbeat();

    uint16_t savedCount = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        SessionLogEntry entry;
        entry.kind = isRefused ? SessionLogEntry::Kind::SAVE_REFUSED : SessionLogEntry::Kind::SAVED;
        addSessionLogEntry(entry);
        savedCount = status.unsavedChangeCount;

        if (!isRefused) {
            status.unsavedChangeCount = 0;
        }
    }

    if (isRefused) {
        reportFlashRefusal("save", "1010:01", saveStatus, current.busVoltageDeciV);
    } else if (preview.keepsBatteryUndervoltage || preview.lowersRatedVoltage) {
        setMessage(MessageLevel::WARNING,
                   "Saved %u change%s, with battery values (undervoltage %" PRIu32 " V%s). Save on a 24 V supply to clear them",
                   savedCount,
                   savedCount == 1 ? "" : "s",
                   preview.keepsBatteryUndervoltage ? preview.batteryUndervoltageV : preview.ownUndervoltageV,
                   preview.lowersRatedVoltage ? ", rated voltage 20 V" : "");
    } else {
        setMessage(MessageLevel::INFO, "Saved %u change%s to the drive", savedCount, savedCount == 1 ? "" : "s");
    }

    ESP_LOGI(TAG, "Save %s: %u changes, bus %" PRId32 " dV", isRefused ? "refused" : "done", savedCount, current.busVoltageDeciV);
    requestReadParameters();
}

bool MotorService::restoreDefaults() {
    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    // Never under a running or holding motor: the drive reloads every setting, including its limits and mode
    if (!current.isConnected || current.isRunning || isPowered) {
        setMessage(MessageLevel::WARNING, "Restore refused: %s", current.isConnected ? "switch the motor off first" : "no drive");

        return false;
    }

    // The drive only writes its flash with the bus at about 80 % of its rated voltage 2000:07 (24 V: 19.2 V, bench
    // 2026-10-11). On a pack below that, lower 2000:07 at run time first; the restore reloads the factory value itself
    uint32_t ratedVoltageV = 0;

    if (sdo.read(Objects::RATED_VOLTAGE, ratedVoltageV) == SdoResult::OK && current.busVoltageDeciV < static_cast<int32_t>(ratedVoltageV) * FLASH_WRITE_MIN_PERCENT / 10) {
        writeObject(Objects::RATED_VOLTAGE, RESTORE_RATED_VOLTAGE_V, "Rated voltage for the restore");
        ESP_LOGI(TAG, "Rated voltage 2000:07 lowered from %" PRIu32 " V to %" PRIu32 " V for the restore (bus %" PRId32 " dV)", ratedVoltageV, RESTORE_RATED_VOLTAGE_V, current.busVoltageDeciV);
    }

    // The restore itself: 1011:01 with "load", nothing else (safety rules)
    if (!writeObject(Objects::RESTORE_DEFAULTS, RESTORE_COMMAND_LOAD, "Restore defaults")) {
        return false;
    }

    lastFlashWriteUs = esp_timer_get_time();
    setMessage(MessageLevel::INFO, "Restore sent, checking");

    // Accepted isn't done: a drive that can't write its flash answers with a save fault, active in 603F. Read it rather
    // than wait for the EMCY, which doesn't come again while the warning is still active from an earlier attempt
    for (uint32_t waitedMs = 0; waitedMs < RESTORE_CHECK_DELAY_MS; waitedMs += SAMPLE_INTERVAL_MS) {
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    uint32_t errorCode = 0;
    bool isRefused = sdo.read(Objects::ERROR_CODE, errorCode) == SdoResult::OK && errorCode == AlarmCodes::SAVE_FAULT;

    // The drive's own verdict: 1011:01 reads 1 after a restore it did, 2 after one it refused on firmware V3.2 (manual
    // §7.6.2, motor 206233, D-031). A blank flash refused every restore without the console noticing until this
    uint32_t restoreStatus = 1;

    if (sdo.read(Objects::RESTORE_DEFAULTS, restoreStatus) == SdoResult::OK && restoreStatus != 1) {
        isRefused = true;
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        SessionLogEntry entry;
        entry.kind = isRefused ? SessionLogEntry::Kind::RESTORE_REFUSED : SessionLogEntry::Kind::RESTORED;
        addSessionLogEntry(entry);
    }

    if (isRefused) {
        reportFlashRefusal("restore", "1011:01", restoreStatus, current.busVoltageDeciV);
        ESP_LOGW(TAG, "Restore refused by the drive: 603F = 0x%04" PRIX32 ", 1011:01 = %" PRIu32 " at bus %" PRId32 " dV", errorCode, restoreStatus, current.busVoltageDeciV);

        return false;
    }

    // The drive reloads its factory settings into the running drive at once (bench 2026-10-11): the console's session
    // settings are gone too, the heartbeat among them (D-023). Apply them again, as at connect, from the new values
    lowerUndervoltageLimitIfNeeded();
    enableHeartbeat();

    // Settings marked "restart activate" (node ID, bitrate and a few more) only load at start-up
    setMessage(MessageLevel::INFO, "Defaults restored. Power-cycle the motor so settings that load at start-up take effect too");

    // Run-time changes are gone with the restore; said quietly, after the restore's own message. A recovered drive now
    // holds its own values again (D-031), and the sentinels say whether the flash is still blank
    isFakeBlank = false;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.unsavedChangeCount = 0;
        status.isRecoveredNotRestored = false;
    }

    readRunCheckValues();
    isFaultRecordStale = true;
    requestReadParameters();

    return true;
}

void MotorService::applySearchStep(uint32_t step) {
    // The other bitrates from the drive's table (200C:03, manual §3.6.1), fastest first
    static constexpr uint32_t SEARCH_BITRATES[SEARCH_BITRATE_COUNT] = {800000, 500000, 250000, 125000, 100000, 50000, 20000, 10000};

    uint8_t nodeId = BoardConfig::DRIVE_NODE_ID;
    uint32_t bitrate = BoardConfig::CAN_BITRATE;

    if (step >= 1 && step <= SEARCH_BITRATE_COUNT) {
        bitrate = SEARCH_BITRATES[step - 1];
    } else if (step > SEARCH_BITRATE_COUNT) {
        nodeId = static_cast<uint8_t>(step - SEARCH_BITRATE_COUNT + 1);
    }

    // A bitrate the controller can't do is skipped: the next step tries again
    bool isBusReady = bus.setBitrate(bitrate);
    sdo.setNodeId(nodeId);
    sdo.setTimeoutMs(step == 0 ? BoardConfig::SDO_TIMEOUT_MS : SEARCH_SDO_TIMEOUT_MS);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.busNodeId = nodeId;
        status.busBitrate = bus.getBitrate();
    }

    if (!isBusReady) {
        ESP_LOGW(TAG, "Search: %" PRIu32 " bit/s not available", bitrate);

        return;
    }

    // At each bitrate once: a drive left in NMT Stopped by another master ignores SDO, so put every node in
    // pre-operational first (a network command, nothing stored; what any CANopen master does at start-up)
    if (step <= SEARCH_BITRATE_COUNT) {
        sdo.sendNmtToAll(SdoClient::NMT_ENTER_PRE_OPERATIONAL);
    }
}

void MotorService::captureOwnParameterValues() {
    static ParameterValue ownValues[Parameters::ENTRY_COUNT]; // Motor task only; too big for its stack
    std::fill(ownValues, ownValues + Parameters::ENTRY_COUNT, ParameterValue {});
    size_t capturedCount = 0;

    // The motor's own values, before the console writes anything this session. A motor that wasn't power-cycled since
    // the console's last session still holds that session's values, so those show as its own (PAR-002)
    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[position];
        bool isConsoleWritten = (entry.flags & (Parameters::Flag::SESSION | Parameters::Flag::CONSOLE_OWNED)) != 0;

        if (!isConsoleWritten || entry.access == Parameters::Access::WRITE_ONLY) {
            continue;
        }

        uint32_t value = 0;

        if (sdo.read(entry.index, entry.subIndex, value) == SdoResult::OK) {
            ownValues[position].ownRaw = value;
            ownValues[position].hasOwnValue = true;
            capturedCount++;
        }
    }

    // The console's heartbeat already on means the motor kept a previous console session's values (it wasn't
    // power-cycled, for example after the console was reflashed): what was just captured isn't the motor's own. The
    // PTU firmware never turns the heartbeat on, and its factory value is 0
    const Parameters::Entry* heartbeat = Parameters::find(Objects::HEARTBEAT_PRODUCER_MS.index, Objects::HEARTBEAT_PRODUCER_MS.subIndex);
    size_t heartbeatPosition = heartbeat != nullptr ? static_cast<size_t>(heartbeat - Parameters::ENTRIES) : 0;
    bool hasEarlierSessionValues = heartbeat != nullptr && ownValues[heartbeatPosition].hasOwnValue && ownValues[heartbeatPosition].ownRaw == HEARTBEAT_INTERVAL_MS;

    if (hasEarlierSessionValues) {
        ESP_LOGW(TAG, "Motor kept an earlier console session's settings (heartbeat already %u ms): its own values are unknown until a power cycle", HEARTBEAT_INTERVAL_MS);
    }

    // A new drive: the last drive's values go
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        std::copy(ownValues, ownValues + Parameters::ENTRY_COUNT, parameterValues);
        status.hasEarlierSessionValues = hasEarlierSessionValues;
        status.parametersRead = 0;
        status.parametersTotal = 0;
        status.isReadingParameters = false;
        status.parametersVersion++;
    }

    isParameterReadActive = false;
    ESP_LOGI(TAG, "Own values captured for %u parameters", static_cast<unsigned>(capturedCount));
}

void MotorService::startParameterRead(int32_t group) {
    size_t total = 0;

    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        bool isInGroup = group == ALL_PARAMETERS || Parameters::ENTRIES[position].group == group;

        if (isInGroup && Parameters::ENTRIES[position].access != Parameters::Access::WRITE_ONLY) {
            total++;
        }
    }

    parameterReadCursor = 0;
    parameterReadGroup = group;
    parameterReadStartUs = esp_timer_get_time();
    parameterReadErrorCount = 0;
    isParameterReadActive = true;

    std::lock_guard<std::mutex> lock(statusMutex);
    status.parametersRead = 0;
    status.parametersTotal = static_cast<uint16_t>(total);
    status.isReadingParameters = true;
}

void MotorService::readParameterChunk() {
    size_t readCount = 0;
    bool isRunning = false;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        isRunning = status.isRunning;
    }

    // Small chunks while running, so the motion samples keep their pace; larger when stopped, for a quicker read
    size_t chunkSize = isRunning ? PARAMETER_READ_CHUNK_RUNNING : PARAMETER_READ_CHUNK_STOPPED;

    while (parameterReadCursor < Parameters::ENTRY_COUNT && readCount < chunkSize) {
        size_t position = parameterReadCursor++;
        const Parameters::Entry& entry = Parameters::ENTRIES[position];
        bool isInGroup = parameterReadGroup == ALL_PARAMETERS || entry.group == parameterReadGroup;

        // Write-only objects abort a read (60C4:05/:06); the table says what they are
        if (!isInGroup || entry.access == Parameters::Access::WRITE_ONLY) {
            continue;
        }

        uint32_t value = 0;
        SdoResult result = sdo.read(entry.index, entry.subIndex, value);
        readCount++;

        {
            std::lock_guard<std::mutex> lock(statusMutex);
            ParameterValue& stored = parameterValues[position];
            stored.result = result;
            stored.isRead = true;
            stored.abortCode = result == SdoResult::ABORTED ? sdo.lastAbortCode : 0;

            if (result == SdoResult::OK) {
                stored.raw = value;
            }

            status.parametersRead++;
        }

        if (result != SdoResult::OK) {
            parameterReadErrorCount++;
        }

        // No answer: the drive is gone or the bus is down, which the poll notices; stop rather than time out on every
        // remaining entry
        if (result == SdoResult::TIMEOUT || result == SdoResult::SEND_FAILED) {
            ESP_LOGW(TAG, "Parameter read stopped at %04X:%02X: %s", entry.index, entry.subIndex, SdoClient::getResultName(result));
            parameterReadCursor = Parameters::ENTRY_COUNT;

            break;
        }
    }

    if (parameterReadCursor < Parameters::ENTRY_COUNT) {
        return;
    }

    // Done
    isParameterReadActive = false;
    int64_t elapsedMs = (esp_timer_get_time() - parameterReadStartUs) / 1000;
    uint16_t readTotal = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isReadingParameters = false;
        status.parametersVersion++;
        readTotal = status.parametersRead;
    }

    ESP_LOGI(TAG, "Parameters: %u read in %" PRId64 " ms, %u errors", readTotal, elapsedMs, static_cast<unsigned>(parameterReadErrorCount));

    // The serial "params" command: every value, to compare with the factory dump
    if (shouldLogParameters.exchange(false)) {
        logParameterValues();
    }
}

void MotorService::writeParameter(size_t position, uint32_t raw, bool isRevert) {
    const Parameters::Entry& entry = Parameters::ENTRIES[position];
    MotorStatus current = getStatus();
    const char* name = entry.description[0] != '\0' ? entry.description : entry.name;
    char text[48];

    // The UI offers only these; checked again here, where the write happens. Node ID and bitrate come from the Setup
    // danger group (PAR-008); a revert may put a locked constant back after a calibration (D-032)
    bool isBusSetting = isBusSettingEntry(entry);

    if (!ParameterFormat::isEditable(entry) && !isBusSetting && !isRevert) {
        setMessage(MessageLevel::ERROR, "%s (%04X:%02X) can't be changed here", name, entry.index, entry.subIndex);

        return;
    }

    if (isBusSetting && !canWriteBusSetting(entry, raw)) {
        return;
    }

    if (current.isRunning && !ParameterFormat::isEditableWhileRunning(entry)) {
        setMessage(MessageLevel::WARNING, "Stop the motor to change %s: it applies %s", name, ParameterFormat::getEffectText(entry.effect));

        return;
    }

    // The value before, for the unsaved changes: from the last read, or read now
    uint32_t before = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        before = parameterValues[position].raw;
    }

    if (!parameterValues[position].isRead || parameterValues[position].result != SdoResult::OK) {
        sdo.read(entry.index, entry.subIndex, before);
    }

    SdoResult result = sdo.write(entry.index, entry.subIndex, raw, ParameterFormat::getSizeBytes(entry));

    if (result != SdoResult::OK) {
        if (result == SdoResult::ABORTED) {
            setMessage(MessageLevel::ERROR, "%s not written: %s (abort 0x%08" PRIX32 ")", name, SdoClient::getAbortCodeDescription(sdo.lastAbortCode), sdo.lastAbortCode);
        } else {
            setMessage(MessageLevel::ERROR, "%s not written: %s", name, SdoClient::getResultName(result));
        }

        ESP_LOGW(TAG, "Write %04X:%02X = 0x%08" PRIX32 " failed", entry.index, entry.subIndex, raw);

        return;
    }

    // What the drive holds now: it may round or ignore a value without an abort
    uint32_t now = raw;
    SdoResult readResult = sdo.read(entry.index, entry.subIndex, now);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        ParameterValue& stored = parameterValues[position];
        stored.raw = now;
        stored.result = readResult;
        stored.isRead = true;
        recordUnsavedChange(position, before, now);
        status.parametersVersion++;
    }

    // The Torque screen shows the torque speed limits; follow a change to them
    if (readResult == SdoResult::OK && entry.index == Objects::TORQUE_SPEED_LIMIT_FORWARD.index &&
        (entry.subIndex == Objects::TORQUE_SPEED_LIMIT_FORWARD.subIndex || entry.subIndex == Objects::TORQUE_SPEED_LIMIT_REVERSE.subIndex)) {
        readTorqueSpeedLimits();
    }

    // A new quick-stop deceleration moves the ramp limit at once (D-019), not at the next connect
    if (readResult == SdoResult::OK && entry.index == Objects::QUICK_STOP_DECELERATION.index && entry.subIndex == Objects::QUICK_STOP_DECELERATION.subIndex) {
        applyQuickStopLimit(now);
    }

    ParameterFormat::formatValue(entry, now, current.unitsPerTurn, text, sizeof(text));
    ESP_LOGI(TAG, "Wrote %04X:%02X: 0x%08" PRIX32 " -> 0x%08" PRIX32 " (read back 0x%08" PRIX32 ")", entry.index, entry.subIndex, before, raw, now);

    if (readResult == SdoResult::OK && now != raw) {
        setMessage(MessageLevel::WARNING, "%s written, but the drive reads back %s", name, text);
    } else if (entry.effect == Parameters::Effect::IMMEDIATE) {
        setMessage(MessageLevel::INFO, "%s set to %s; not saved", name, text);
    } else {
        setMessage(MessageLevel::INFO, "%s set to %s, applies %s; not saved", name, text, ParameterFormat::getEffectText(entry.effect));
    }
}

void MotorService::startStepTest() {
    MotorStatus current = getStatus();

    if (current.isStepTest) {
        return;
    }

    // The motion screens' settings, put back when the test ends
    savedVelocityRpm = targetVelocityRpm;
    savedAccelerationRpmPerSecond = targetAccelerationRpmPerSecond;
    savedIsPositionRelative = isPositionRelative;
    savedMoveByMilliturns = moveByMilliturns;
    savedMoveSpeedRpm = moveSpeedRpm;
    isStepTestAtB = false;
    bool isStarted = false;
    applyStepTestProfile();

    if (isStepTestPosition) {
        // Relative moves there and back: each starts from the last target, so the steps don't drift (D4 of Position mode)
        isPositionRelative = true;
        moveByMilliturns = stepTestStepMilliturns.load();
        isStarted = goPosition();
        isStepTestAtB = true;
    } else {
        targetVelocityRpm = stepTestVelocityARpm.load();
        isStarted = current.isRunning && current.motionMode == MotionMode::VELOCITY ? true : startVelocity();
    }

    if (!isStarted) {
        endStepTest();

        return;
    }

    nextStepUs = esp_timer_get_time() + static_cast<int64_t>(stepTestDwellMs) * 1000;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isStepTest = true;
    }

    if (isStepTestPosition) {
        setMessage(MessageLevel::INFO, "Step test: %" PRId32 " milliturns and back, %" PRId32 " ms at each end", stepTestStepMilliturns.load(), stepTestDwellMs.load());
    } else {
        setMessage(MessageLevel::INFO, "Step test: %" PRId32 " and %" PRId32 " rpm, %" PRId32 " ms each", stepTestVelocityARpm.load(), stepTestVelocityBRpm.load(), stepTestDwellMs.load());
    }

    ESP_LOGI(TAG, "Step test started (%s)", isStepTestPosition ? "position" : "velocity");
}

void MotorService::runStepTest() {
    MotorStatus current = getStatus();

    if (!current.isStepTest) {
        return;
    }

    // STOP, a fault, a stall, low voltage or a lost drive stopped the motor: the test is over
    if (!current.isRunning) {
        endStepTest();
        ESP_LOGI(TAG, "Step test ended: the motor stopped");

        return;
    }

    if (esp_timer_get_time() < nextStepUs) {
        return;
    }

    isStepTestAtB = !isStepTestAtB;
    nextStepUs += static_cast<int64_t>(stepTestDwellMs) * 1000;
    applyStepTestProfile();

    if (current.motionMode == MotionMode::POSITION) {
        int32_t stepMilliturns = stepTestStepMilliturns;
        moveByMilliturns = isStepTestAtB ? stepMilliturns : -stepMilliturns;
        goPosition();
    } else {
        // The velocity loop below writes the new target this pass
        targetVelocityRpm = isStepTestAtB ? stepTestVelocityBRpm.load() : stepTestVelocityARpm.load();
    }
}

void MotorService::stopStepTest() {
    MotorStatus current = getStatus();

    if (!current.isStepTest) {
        return;
    }

    if (current.isRunning && current.motionMode == MotionMode::POSITION) {
        haltAndRelease();
    } else if (current.isRunning) {
        rampStop();
    }

    endStepTest();
    ESP_LOGI(TAG, "Step test stopped");
}

void MotorService::endStepTest() {
    restoreRampLimits();
    targetVelocityRpm = savedVelocityRpm;
    targetAccelerationRpmPerSecond = savedAccelerationRpmPerSecond;
    isPositionRelative = savedIsPositionRelative;
    moveByMilliturns = savedMoveByMilliturns;
    moveSpeedRpm = savedMoveSpeedRpm;

    std::lock_guard<std::mutex> lock(statusMutex);
    status.isStepTest = false;
}

void MotorService::applyStepTestProfile() {
    int32_t rampRpmPerSecond = getStepTestAccelerationRpmPerSecond();

    // The drive caps the ramps at 60C5 and 60C6: raise them (and 6085, so STOP stays the steepest) when the ramp needs it
    if (!raiseRampLimits(static_cast<uint32_t>(rpmToCountsPerSecond(rampRpmPerSecond)))) {
        rampRpmPerSecond = std::min(rampRpmPerSecond, accelerationLimitRpmPerSecond.load());
    }

    // Velocity mode picks the acceleration up in the task loop, position mode at each GO
    targetAccelerationRpmPerSecond = rampRpmPerSecond;
    moveSpeedRpm = getStepTestMoveSpeedRpm();
}

bool MotorService::raiseRampLimits(uint32_t rampCounts) {
    // Read once per test, before anything is raised
    if (!hasRaisedRampLimits) {
        bool isRead = sdo.read(Objects::MAX_ACCELERATION, savedMaxAccelerationCounts) == SdoResult::OK && sdo.read(Objects::MAX_DECELERATION, savedMaxDecelerationCounts) == SdoResult::OK &&
                      sdo.read(Objects::QUICK_STOP_DECELERATION, savedQuickStopCounts) == SdoResult::OK;

        if (!isRead) {
            setMessage(MessageLevel::WARNING, "Step test: couldn't read the drive's ramp limits; ramp kept within them");

            return false;
        }

        hasRaisedRampLimits = true;
        raisedRampCounts = std::min({savedMaxAccelerationCounts, savedMaxDecelerationCounts, savedQuickStopCounts});
    }

    if (rampCounts <= raisedRampCounts) {
        return true;
    }

    // Quick stop first, so STOP is never gentler than the ramps at any moment
    bool isWritten = writeObject(Objects::QUICK_STOP_DECELERATION, std::max(rampCounts, savedQuickStopCounts), "Quick-stop deceleration for the test") &&
                     writeObject(Objects::MAX_DECELERATION, std::max(rampCounts, savedMaxDecelerationCounts), "Max deceleration for the test") &&
                     writeObject(Objects::MAX_ACCELERATION, std::max(rampCounts, savedMaxAccelerationCounts), "Max acceleration for the test");

    if (!isWritten) {
        return false;
    }

    raisedRampCounts = rampCounts;
    ESP_LOGI(TAG,
             "Step test: 60C5, 60C6 and 6085 at least %" PRIu32 " units/s² (were %" PRIu32 ", %" PRIu32 ", %" PRIu32 ")",
             rampCounts,
             savedMaxAccelerationCounts,
             savedMaxDecelerationCounts,
             savedQuickStopCounts);

    return true;
}

void MotorService::restoreRampLimits() {
    if (!hasRaisedRampLimits) {
        return;
    }

    hasRaisedRampLimits = false;

    // Ramps down first, then the quick stop, so STOP is never gentler than the ramps at any moment
    bool isRestored = writeObject(Objects::MAX_ACCELERATION, savedMaxAccelerationCounts, "Max acceleration back") &&
                      writeObject(Objects::MAX_DECELERATION, savedMaxDecelerationCounts, "Max deceleration back") &&
                      writeObject(Objects::QUICK_STOP_DECELERATION, savedQuickStopCounts, "Quick-stop deceleration back");

    if (isRestored) {
        ESP_LOGI(TAG, "Step test: ramp limits back to %" PRIu32 ", %" PRIu32 ", %" PRIu32, savedMaxAccelerationCounts, savedMaxDecelerationCounts, savedQuickStopCounts);
    }
}

void MotorService::zeroHere() {
    // The position as it is now, rather than the last sample
    int32_t positionUnits = 0;

    if (sdo.readSigned(Objects::POSITION_ACTUAL_USER, positionUnits) != SdoResult::OK) {
        setMessage(MessageLevel::WARNING, "Zero not set: the position couldn't be read");

        return;
    }

    int32_t previousMilliturns = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        previousMilliturns = status.positionMilliturns;
        status.positionZeroUnits = positionUnits;
        setPosition(positionUnits);
    }

    setMessage(
        MessageLevel::INFO, "Zero set here (was %s%" PRId32 ".%03" PRId32 " turns)", previousMilliturns < 0 ? "-" : "", std::abs(previousMilliturns) / 1000, std::abs(previousMilliturns) % 1000);
    ESP_LOGI(TAG, "Console zero at %" PRId32 " units", positionUnits);
}

void MotorService::revertParameters() {
    UnsavedChange changes[MAX_UNSAVED_CHANGES];
    size_t changeCount = getUnsavedChanges(changes, MAX_UNSAVED_CHANGES);
    bool isRunning = getStatus().isRunning;
    size_t revertedCount = 0;
    size_t leftCount = 0;
    const char* leftName = nullptr;

    // Each one back through the normal write, which drops it from the unsaved changes once it is at its earlier value
    for (size_t index = 0; index < changeCount; index++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[changes[index].position];

        if (isRunning && !ParameterFormat::isEditableWhileRunning(entry)) {
            leftCount++;
            leftName = entry.description[0] != '\0' ? entry.description : entry.name;

            continue;
        }

        writeParameter(changes[index].position, changes[index].before, true);
        revertedCount++;
        keepAlive();
    }

    // The summary replaces the last write's message; a failed write stays in the list and in the log
    size_t remainingCount = getStatus().unsavedChangeCount;

    if (leftCount > 0) {
        setMessage(MessageLevel::WARNING, "Reverted %u; %u left until the motor stops (%s)", static_cast<unsigned>(revertedCount), static_cast<unsigned>(leftCount), leftName);
    } else if (remainingCount > 0) {
        setMessage(MessageLevel::WARNING, "Reverted %u; %u could not be written back", static_cast<unsigned>(changeCount - remainingCount), static_cast<unsigned>(remainingCount));
    } else {
        setMessage(MessageLevel::INFO, "Reverted %u change%s", static_cast<unsigned>(revertedCount), revertedCount == 1 ? "" : "s");
    }

    ESP_LOGI(TAG, "Revert: %u written back, %u left while running, %u still unsaved", static_cast<unsigned>(revertedCount), static_cast<unsigned>(leftCount), static_cast<unsigned>(remainingCount));
}

bool MotorService::isBusSettingEntry(const Parameters::Entry& entry) {
    bool isNodeId = entry.index == Objects::NODE_ID.index && entry.subIndex == Objects::NODE_ID.subIndex;
    bool isBitrate = entry.index == Objects::BITRATE.index && entry.subIndex == Objects::BITRATE.subIndex;

    return isNodeId || isBitrate;
}

bool MotorService::canWriteBusSetting(const Parameters::Entry& entry, uint32_t raw) {
    MotorStatus current = getStatus();
    bool isPowered = current.state == DriveState::OPERATION_ENABLED || current.state == DriveState::QUICK_STOP_ACTIVE;

    if (current.isRunning || isPowered) {
        setMessage(MessageLevel::WARNING, "Stop the motor before changing the node ID or bitrate");

        return false;
    }

    bool isNodeId = entry.index == Objects::NODE_ID.index && entry.subIndex == Objects::NODE_ID.subIndex;

    if (isNodeId && (raw < 1 || raw > MAX_NODE_ID)) {
        setMessage(MessageLevel::ERROR, "Node ID %" PRIu32 " is outside 1-%u", raw, static_cast<unsigned>(MAX_NODE_ID));

        return false;
    }

    if (!isNodeId && raw > BITRATE_CODE_1M) {
        setMessage(MessageLevel::ERROR, "Bitrate code %" PRIu32 " is outside 0-%" PRIu32, raw, BITRATE_CODE_1M);

        return false;
    }

    // The other one, as the drive will have it after the power cycle; the search (D-026) finds a motor only when one of
    // the two is at the default, so only one may move away from it (PAR-008)
    uint32_t otherRaw = 0;
    const ObjectEntry& other = isNodeId ? Objects::BITRATE : Objects::NODE_ID;
    SdoResult result = sdo.read(other, otherRaw);

    if (result != SdoResult::OK) {
        setMessage(MessageLevel::ERROR, "Not written: couldn't read %04X:%02X (%s)", other.index, other.subIndex, SdoClient::getResultName(result));

        return false;
    }

    bool isOtherDefault = isNodeId ? otherRaw == BITRATE_CODE_1M : otherRaw == BoardConfig::DRIVE_NODE_ID;
    bool isDefault = isNodeId ? raw == BoardConfig::DRIVE_NODE_ID : raw == BITRATE_CODE_1M;

    if (!isDefault && !isOtherDefault) {
        setMessage(MessageLevel::WARNING,
                   isNodeId ? "Set the bitrate back to 1 Mbit/s first: the console finds a motor only when one of the two is at its default"
                            : "Set the node ID back to 1 first: the console finds a motor only when one of the two is at its default");

        return false;
    }

    return true;
}

void MotorService::recordUnsavedChange(size_t position, uint32_t before, uint32_t now) {
    size_t count = status.unsavedChangeCount;

    for (size_t index = 0; index < count; index++) {
        if (unsavedChanges[index].position != position) {
            continue;
        }

        // Back at the value before the first write: nothing to save
        if (now == unsavedChanges[index].before) {
            unsavedChanges[index] = unsavedChanges[count - 1];
            status.unsavedChangeCount--;
        } else {
            unsavedChanges[index].now = now;
        }

        return;
    }

    if (now == before || count >= MAX_UNSAVED_CHANGES) {
        return;
    }

    unsavedChanges[count] = {static_cast<uint16_t>(position), before, now};
    status.unsavedChangeCount++;
}

void MotorService::clearUnsavedChanges(const char* reason) {
    uint16_t count = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        count = status.unsavedChangeCount;
        status.unsavedChangeCount = 0;
    }

    if (count > 0) {
        setMessage(MessageLevel::WARNING, "%u unsaved parameter change%s dropped: %s", count, count == 1 ? "" : "s", reason);
    }
}

void MotorService::logParameterValues() {
    static ParameterValue values[Parameters::ENTRY_COUNT]; // Motor task only; too big for its stack
    getParameterValues(values, Parameters::ENTRY_COUNT);

    for (size_t position = 0; position < Parameters::ENTRY_COUNT; position++) {
        const Parameters::Entry& entry = Parameters::ENTRIES[position];
        const ParameterValue& value = values[position];

        if (!value.isRead) {
            continue;
        }

        if (value.result == SdoResult::ABORTED) {
            ESP_LOGI(TAG, "PARAM %04X:%02X aborted 0x%08" PRIX32 " (%s) %s", entry.index, entry.subIndex, value.abortCode, SdoClient::getAbortCodeDescription(value.abortCode), entry.name);
        } else if (value.result != SdoResult::OK) {
            ESP_LOGI(TAG, "PARAM %04X:%02X %s %s", entry.index, entry.subIndex, SdoClient::getResultName(value.result), entry.name);
        } else if (value.hasOwnValue) {
            ESP_LOGI(TAG, "PARAM %04X:%02X = 0x%08" PRIX32 " (own 0x%08" PRIX32 ") %s", entry.index, entry.subIndex, value.raw, value.ownRaw, entry.name);
        } else {
            ESP_LOGI(TAG, "PARAM %04X:%02X = 0x%08" PRIX32 " %s", entry.index, entry.subIndex, value.raw, entry.name);
        }

        keepAlive();
    }
}

void MotorService::readRunCheckValues() {
    RunCheckValues values;
    uint32_t value = 0;

    // Each read is best effort; a missing object leaves 0, which no check warns about
    if (sdo.read(Objects::CONTROL_MODE, value) == SdoResult::OK) {
        values.controlMode = static_cast<uint16_t>(value);
    }

    for (size_t input = 0; input < RunCheckValues::INPUT_COUNT; input++) {
        // Function and logic alternate per input, so DI1 :03, DI2 :05, DI3 :07 (manual examples, §7.1.1)
        uint8_t subIndex = static_cast<uint8_t>(Objects::INPUT_FUNCTION_DI1.subIndex + input * 2);

        if (sdo.read(Objects::INPUT_FUNCTION_DI1.index, subIndex, value) == SdoResult::OK) {
            values.inputFunctions[input] = static_cast<uint16_t>(value);
        }
    }

    if (sdo.read(Objects::INPUT_STATES, value) == SdoResult::OK) {
        values.inputStates = static_cast<uint16_t>(value);
    }

    if (sdo.read(Objects::HOMING_METHOD, value) == SdoResult::OK) {
        values.homingMethod = static_cast<uint8_t>(value);
    }

    // Blank-flash sentinels (D-031): settings that never legitimately read all ones; all four did on motor 206233
    static const ObjectEntry* const SENTINELS[] = {&Objects::RATED_CURRENT, &Objects::POLE_PAIRS, &Objects::ENCODER_BITS, &Objects::INPUT_FUNCTION_DI1};
    bool isEverySentinelBlank = true;

    for (const ObjectEntry* sentinel : SENTINELS) {
        uint32_t allOnes = sentinel->sizeBytes >= 4 ? 0xFFFFFFFF : (1u << (sentinel->sizeBytes * 8)) - 1;

        if (sdo.read(*sentinel, value) != SdoResult::OK || (value & allOnes) != allOnes) {
            isEverySentinelBlank = false;
            break;
        }
    }

    values.isParameterFlashBlank = isEverySentinelBlank || isFakeBlank;

    if (values.isParameterFlashBlank) {
        ESP_LOGW(TAG, "Parameter flash is blank%s: the sentinels read all ones", isFakeBlank ? " (faked)" : "");
    }

    values.isRead = true;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.runChecks = values;
    }

    ESP_LOGI(TAG,
             "Run checks: control mode %u, DI functions %u/%u/%u, input states 0x%X, homing method %u",
             values.controlMode,
             values.inputFunctions[0],
             values.inputFunctions[1],
             values.inputFunctions[2],
             values.inputStates,
             values.homingMethod);
}

void MotorService::readFaultRecord() {
    FaultRecord record;
    record.isRead = true;
    isFaultRecordStale = false;

    uint32_t errorRegister = 0;
    sdo.read(Objects::ERROR_REGISTER, errorRegister);
    record.errorRegister = static_cast<uint8_t>(errorRegister);

    // Alarm history, newest first; an empty slot reads 0 (1003:00 is not a count on this firmware)
    for (size_t entry = 0; entry < FaultRecord::HISTORY_LENGTH; entry++) {
        uint32_t word = 0;
        SdoResult result = sdo.read(Objects::ALARM_HISTORY.index, static_cast<uint8_t>(Objects::ALARM_HISTORY.subIndex + entry), word);

        if (result != SdoResult::OK) {
            if (result == SdoResult::ABORTED) {
                snprintf(record.readError,
                         sizeof(record.readError),
                         "1003:%02X aborted 0x%08" PRIX32 " (%s)",
                         static_cast<unsigned>(entry + 1),
                         sdo.lastAbortCode,
                         SdoClient::getAbortCodeDescription(sdo.lastAbortCode));
            } else {
                snprintf(record.readError, sizeof(record.readError), "1003:%02X %s", static_cast<unsigned>(entry + 1), SdoClient::getResultName(result));
            }

            break;
        }

        record.history[entry] = word;

        if (word != 0) {
            record.historyCount = entry + 1;
        }
    }

    keepAlive();

    // Fault reactions: 200E:00 is the number of entries
    uint32_t reactionCount = 0;
    sdo.read(Objects::FAULT_REACTIONS.index, 0, reactionCount);
    reactionCount = std::min<uint32_t>(reactionCount, FaultRecord::MAX_REACTION_COUNT);

    for (uint32_t entry = 0; entry < reactionCount; entry++) {
        uint32_t word = 0;

        if (sdo.read(Objects::FAULT_REACTIONS.index, static_cast<uint8_t>(Objects::FAULT_REACTIONS.subIndex + entry), word) != SdoResult::OK) {
            break;
        }

        record.reactions[entry] = word;
        record.reactionCount = entry + 1;

        uint32_t defaultWord = 0;

        if (AlarmWord::findFactoryDefault(AlarmWord::getCode(word), defaultWord) && defaultWord != word) {
            record.changedReactionCount++;
        }
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.faultRecord = record;
        status.faultRecordVersion++;
    }

    ESP_LOGI(TAG,
             "Fault record: %u history entries, newest 0x%08" PRIX32 ", error register 0x%02X, %u reactions%s%s",
             static_cast<unsigned>(record.historyCount),
             record.history[0],
             record.errorRegister,
             static_cast<unsigned>(record.reactionCount),
             record.readError[0] != '\0' ? ", " : "",
             record.readError);
}

void MotorService::lowerUndervoltageLimitIfNeeded() {
    uint32_t originalLimitV = 0;

    if (sdo.read(Objects::BUS_UNDERVOLTAGE_LIMIT_V, originalLimitV) != SdoResult::OK) {
        setMessage(MessageLevel::WARNING, "Could not read the undervoltage limit (2001:10)");

        return;
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.undervoltageLimitV = originalLimitV;
        status.originalUndervoltageLimitV = originalLimitV;
    }

    if (originalLimitV <= UNDERVOLTAGE_LIMIT_FOR_BATTERY_V) {
        return;
    }

    // Run-time write, not saved: reverts when the motor is power-cycled. The drive applies it when stopped, which it is
    // after connect() switched it off
    if (!writeObject(Objects::BUS_UNDERVOLTAGE_LIMIT_V, UNDERVOLTAGE_LIMIT_FOR_BATTERY_V, "Undervoltage limit")) {
        return;
    }

    uint32_t limitV = 0;
    sdo.read(Objects::BUS_UNDERVOLTAGE_LIMIT_V, limitV);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.undervoltageLimitV = limitV;
    }

    if (limitV != UNDERVOLTAGE_LIMIT_FOR_BATTERY_V) {
        setMessage(MessageLevel::WARNING, "Undervoltage limit write accepted, but it reads back %" PRIu32 " V", limitV);
    }

    ESP_LOGI(TAG, "Undervoltage limit lowered from %" PRIu32 " V to %" PRIu32 " V for this session", originalLimitV, limitV);
}

bool MotorService::poll() {
    uint32_t statusword = 0;

    // The statusword decides whether the drive is still there
    if (sdo.read(Objects::STATUSWORD, statusword) != SdoResult::OK) {
        return false;
    }

    // The rest is best effort: a failed read keeps the previous value rather than showing a made-up zero
    MotorStatus previous = getStatus();
    uint32_t errorCode = previous.errorCode;
    int32_t velocityRpm = previous.velocityRpm;
    int32_t positionCounts = previous.positionCounts;
    int32_t positionUnits = previous.positionUnits;
    int32_t currentPerMille = previous.currentPerMille;
    int32_t busVoltageDeciV = previous.busVoltageDeciV;
    int32_t temperatureC = previous.temperatureC;
    sdo.read(Objects::ERROR_CODE, errorCode);
    sdo.readSigned(Objects::VELOCITY_ACTUAL, velocityRpm);
    sdo.readSigned(Objects::POSITION_ACTUAL, positionCounts);
    sdo.readSigned(Objects::POSITION_ACTUAL_USER, positionUnits);
    sdo.readSigned(Objects::CURRENT_ACTUAL, currentPerMille);
    sdo.readSigned(Objects::BUS_VOLTAGE, busVoltageDeciV);
    sdo.readSigned(Objects::TEMPERATURE, temperatureC);

    std::lock_guard<std::mutex> lock(statusMutex);

    // A new error code may have added to the history even without an EMCY seen
    if (static_cast<uint16_t>(errorCode) != status.errorCode) {
        isFaultRecordStale = true;
        ESP_LOGI(TAG, "Error code 0x%04X -> 0x%04" PRIX32 ", statusword 0x%04" PRIX32, status.errorCode, errorCode, statusword);
    }

    DriveState previousState = status.state;
    status.statusword = static_cast<uint16_t>(statusword);
    status.state = decodeDriveState(status.statusword);

    if (status.state != previousState) {
        ESP_LOGI(TAG, "State %s -> %s, statusword 0x%04X", getDriveStateName(previousState), getDriveStateName(status.state), status.statusword);
    }
    status.errorCode = static_cast<uint16_t>(errorCode);
    status.velocityRpm = velocityRpm;
    status.positionCounts = positionCounts;
    setPosition(positionUnits);
    setCurrent(currentPerMille);
    status.busVoltageDeciV = busVoltageDeciV;
    status.temperatureC = temperatureC;

    // A fault while running ends the run
    if (status.isRunning && status.state == DriveState::FAULT) {
        status.isRunning = false;
        snprintf(status.message, sizeof(status.message), "Drive faulted, error 0x%04X", status.errorCode);
        status.messageLevel = MessageLevel::ERROR;
        status.messageSequence++;
    } else if (status.isRunning && status.state != DriveState::OPERATION_ENABLED && status.state != DriveState::QUICK_STOP_ACTIVE) {
        // The drive switched itself off without a fault, as an undervoltage reaction does (bench 2026-10-12): the run is
        // over. The console's own stops clear isRunning first, so this only sees the drive's
        status.isRunning = false;

        // Name the alarm: the active one, or one reported just before (an undervoltage clears itself at once)
        uint16_t alarmCode = status.errorCode;

        if (alarmCode == 0 && lastAlarmCode != 0 && esp_timer_get_time() - lastAlarmUs < static_cast<int64_t>(RECENT_ALARM_MS) * 1000) {
            alarmCode = lastAlarmCode;
        }

        const AlarmCode* alarm = alarmCode != 0 ? AlarmCodes::find(alarmCode) : nullptr;

        if (alarmCode != 0) {
            snprintf(status.message, sizeof(status.message), "Drive switched itself off: 0x%04X %s", alarmCode, alarm != nullptr ? alarm->name : "(unknown code)");
        } else {
            snprintf(status.message, sizeof(status.message), "Drive switched itself off (%s, statusword 0x%04X)", getDriveStateName(status.state), status.statusword);
        }

        status.messageLevel = MessageLevel::WARNING;
        status.messageSequence++;
        ESP_LOGW(TAG, "Run ended: the drive switched itself off, statusword 0x%04X", status.statusword);
    }

    return true;
}

void MotorService::handleLostDrive() {
    std::lock_guard<std::mutex> lock(statusMutex);

    // A parameter read in progress ends; the values stay until the next connect clears them
    isParameterReadActive = false;
    status.isReadingParameters = false;

    // Look for it at the default first again (D-026)
    shouldResetSearch = true;

    // Undervoltage, then silence: the supply was switched off (bench 2026-10-11). The EMCY can come before a poll reads
    // 603F, so either counts
    bool isPoweredOff = status.errorCode == AlarmCodes::UNDERVOLTAGE || status.emcyErrorCode == AlarmCodes::UNDERVOLTAGE;

    // Nothing over CAN reaches the drive now. With its heartbeat unacknowledged it raises 0x7501 and halts (D-023);
    // without the heartbeat it keeps its last setpoint until the cable is back (D-011)
    if (status.isRunning) {
        status.isLostWhileRunning = true;
        snprintf(status.message,
                 sizeof(status.message),
                 "%s",
                 status.heartbeatMs > 0 ? "Lost the drive while running: it should halt itself (0x7501); unplug the motor if it doesn't"
                                        : "Lost the drive while running: it keeps turning until the cable is back or it's unplugged");
        status.messageLevel = MessageLevel::WARNING;
    } else if (isPoweredOff) {
        snprintf(status.message, sizeof(status.message), "Motor powered off, searching");
        status.messageLevel = MessageLevel::INFO;
    } else {
        snprintf(status.message, sizeof(status.message), "Lost the drive, searching");
        status.messageLevel = MessageLevel::WARNING;
    }

    SessionLogEntry entry;
    entry.kind = isPoweredOff ? SessionLogEntry::Kind::POWERED_OFF : SessionLogEntry::Kind::LOST;
    addSessionLogEntry(entry);

    status.isConnected = false;
    status.isRunning = false;
    status.messageSequence++;
    ESP_LOGW(TAG, "%s", status.message);
}

void MotorService::checkPackVoltage() {
    MotorStatus current = getStatus();
    bool isLow = current.busVoltageDeciV > 0 && current.busVoltageDeciV < BoardConfig::PACK_CUTOFF_DECIV;
    bool isRecovered = current.busVoltageDeciV >= BoardConfig::PACK_CUTOFF_DECIV + BoardConfig::PACK_CUTOFF_HYSTERESIS_DECIV;

    // Several readings in a row, so one sample during an acceleration current peak doesn't trip it
    lowVoltagePollCount = isLow ? lowVoltagePollCount + 1 : 0;

    if (lowVoltagePollCount >= LOW_VOLTAGE_POLLS && !current.isBelowPackCutoff) {
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            status.isBelowPackCutoff = true;
        }

        if (current.isRunning) {
            stopMotion("Pack below the cutoff, stopped", MessageLevel::WARNING);
        }

        ESP_LOGW(TAG, "Bus %" PRId32 " dV below the pack cutoff, motion refused", current.busVoltageDeciV);
    } else if (current.isBelowPackCutoff && isRecovered) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isBelowPackCutoff = false;
    }
}

void MotorService::checkStall() {
    MotorStatus current = getStatus();

    if (!current.isRunning || current.errorCode != AlarmCodes::STALL) {
        isStallChecked = false;

        return;
    }

    if (isStallChecked) {
        return;
    }

    isStallChecked = true;

    // The reaction as the drive has it now; if it can't be read, stop to be safe
    uint32_t reaction = 0;
    SdoResult result = sdo.read(Objects::STALL_REACTION, reaction);

    if (result == SdoResult::OK && AlarmWord::getCode(reaction) == AlarmCodes::STALL && AlarmWord::getResponseCode(reaction) != AlarmWord::RESPONSE_HALT_AND_HOLD) {
        ESP_LOGI(TAG, "Stall with reaction 0x%08" PRIX32 " (%s): left to the drive", reaction, AlarmWord::getResponseText(AlarmWord::getResponseCode(reaction)));

        return;
    }

    // Quick stop and power stage off; 603F stays latched until START or RESET FAULT clears it
    stopMotion("Motor stalled (0x2312): power stage off. Free the shaft, then start again", MessageLevel::WARNING);
}

bool MotorService::canStartMotion(const MotorStatus& current) {
    if (current.isBelowPackCutoff) {
        setMessage(MessageLevel::WARNING, "Start refused: pack below the %d.%d V cutoff", BoardConfig::PACK_CUTOFF_DECIV / 10, BoardConfig::PACK_CUTOFF_DECIV % 10);

        return false;
    }

    if (encoderIncrements == 0) {
        setMessage(MessageLevel::ERROR, "Start refused: encoder resolution (608F:01) unknown");

        return false;
    }

    // Blank motor constants and limits: nothing about such a run is predictable (D-031)
    if (current.runChecks.isParameterFlashBlank) {
        setMessage(MessageLevel::ERROR, "Start refused: the parameter flash is blank. Use RECOVER on the Faults screen");

        return false;
    }

    // A drive recovered at run time can commutate wrongly until it boots on the restored values: motor 159709 ran away
    // to 2090 rpm asking for 600 and was fine after a power cycle (bench 2026-10-15)
    if (current.needsPowerCycle) {
        setMessage(MessageLevel::ERROR, "Start refused: power-cycle the motor first, it was recovered this session");

        return false;
    }

    return true;
}

bool MotorService::enableOperation(uint8_t operationMode, const char* modeStepName) {
    MotorStatus current = getStatus();

    // Settings for this run; run-time only, nothing is saved
    bool isConfigured = writeObject(Objects::CONTROL_MODE, 0, "CiA 402 control mode") && applyAcceleration() && writeObject(Objects::MODE_OF_OPERATION, operationMode, modeStepName);

    if (!isConfigured) {
        return false;
    }

    // Clear a fault first, or the state walk can't leave Fault; logged, since it hides what was there (D-024). Also any
    // error code left over: a stall stays latched in 603F without a fault and halts the next run at once (bench 2026-10-11)
    if (current.state == DriveState::FAULT || current.errorCode != 0) {
        sendFaultReset();

        std::lock_guard<std::mutex> lock(statusMutex);
        SessionLogEntry entry;
        entry.kind = SessionLogEntry::Kind::RESET;
        entry.code = current.errorCode;
        entry.resetBy = operationMode == OperationMode::PROFILE_POSITION ? "GO" : "START";
        addSessionLogEntry(entry);
    }

    // CiA 402 state walk
    bool isEnabled = stepTo(Controlword::DISABLE_VOLTAGE, DriveState::SWITCH_ON_DISABLED, "disable voltage") && stepTo(Controlword::SHUTDOWN, DriveState::READY_TO_SWITCH_ON, "shutdown") &&
                     stepTo(Controlword::SWITCH_ON, DriveState::SWITCHED_ON, "switch on") && stepTo(Controlword::ENABLE_OPERATION, DriveState::OPERATION_ENABLED, "enable operation");

    if (!isEnabled) {
        writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Disable after failed start");

        return false;
    }

    return true;
}

bool MotorService::startVelocity() {
    MotorStatus current = getStatus();

    if (!canStartMotion(current)) {
        return false;
    }

    // Another mode in control: normally ended when the screen opened (D-022); end it here too as a guard
    if (!endOtherMode(MotionMode::VELOCITY, "velocity start")) {
        return false;
    }

    setMessage(MessageLevel::INFO, "Starting");

    if (!writeObject(Objects::TARGET_VELOCITY, 0, "Target velocity 0") || !enableOperation(OperationMode::PROFILE_VELOCITY, "Profile velocity mode")) {
        return false;
    }

    // Go
    int32_t velocityRpm = targetVelocityRpm;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = true;
        status.motionMode = MotionMode::VELOCITY;
    }

    if (!writeObject(Objects::TARGET_VELOCITY, static_cast<uint32_t>(rpmToCountsPerSecond(velocityRpm)), "Target velocity")) {
        stopMotion("Stopped after a failed start", MessageLevel::WARNING);

        return false;
    }

    appliedVelocityRpm = velocityRpm;
    setMessage(MessageLevel::INFO, "Running at %" PRId32 " rpm", velocityRpm);
    ESP_LOGI(TAG, "Running at %" PRId32 " rpm", velocityRpm);

    return true;
}

bool MotorService::startTorque() {
    MotorStatus current = getStatus();

    if (!canStartMotion(current)) {
        return false;
    }

    if (!endOtherMode(MotionMode::TORQUE, "torque start")) {
        return false;
    }

    setMessage(MessageLevel::INFO, "Starting torque mode");

    // The manual's order (CN 6.6.5): slope and a zero target first, then the mode and the state walk
    int32_t slope = torqueSlopePerMillePerSecond;

    if (!writeObject(Objects::TORQUE_SLOPE, static_cast<uint32_t>(slope), "Torque slope") || !writeObject(Objects::TARGET_TORQUE, 0, "Target torque 0") ||
        !enableOperation(OperationMode::PROFILE_TORQUE, "Profile torque mode")) {
        return false;
    }

    appliedTorqueSlope = slope;
    appliedTorquePerMille = 0;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = true;
        status.motionMode = MotionMode::TORQUE;
    }

    // The target follows in the task loop (followTorque)
    int32_t torquePerMille = targetTorquePerMille;
    setMessage(MessageLevel::INFO, "Torque mode: %s%" PRId32 ".%" PRId32 " %%", torquePerMille < 0 ? "-" : "", std::abs(torquePerMille) / 10, std::abs(torquePerMille) % 10);
    ESP_LOGI(TAG, "Torque mode started, target %" PRId32 " per mille, slope %" PRId32 " per mille/s", torquePerMille, slope);

    return true;
}

void MotorService::readTorqueSpeedLimits() {
    uint32_t forwardRpm = 0;
    uint32_t reverseRpm = 0;
    sdo.read(Objects::TORQUE_SPEED_LIMIT_FORWARD, forwardRpm);
    sdo.read(Objects::TORQUE_SPEED_LIMIT_REVERSE, reverseRpm);

    std::lock_guard<std::mutex> lock(statusMutex);
    status.torqueSpeedLimitForwardRpm = static_cast<int32_t>(forwardRpm);
    status.torqueSpeedLimitReverseRpm = static_cast<int32_t>(reverseRpm);
}

void MotorService::followTorque() {
    int32_t slope = torqueSlopePerMillePerSecond;

    if (slope != appliedTorqueSlope && writeObject(Objects::TORQUE_SLOPE, static_cast<uint32_t>(slope), "Torque slope")) {
        appliedTorqueSlope = slope;
    }

    int32_t torquePerMille = targetTorquePerMille;

    if (torquePerMille != appliedTorquePerMille && writeObject(Objects::TARGET_TORQUE, static_cast<uint32_t>(torquePerMille), "Target torque")) {
        appliedTorquePerMille = torquePerMille;
        ESP_LOGI(TAG, "Torque target %" PRId32 " per mille", torquePerMille);
    }
}

void MotorService::torqueStop() {
    MotorStatus current = getStatus();

    if (!current.isRunning || current.motionMode != MotionMode::TORQUE) {
        return;
    }

    setMessage(MessageLevel::INFO, "Torque down");

    if (!writeObject(Objects::TARGET_TORQUE, 0, "Target torque 0")) {
        stopMotion("Torque stop failed, quick-stopped", MessageLevel::WARNING);

        return;
    }

    // Wait for the slope to bring the torque to 0: 1.5x the expected time plus a margin
    int32_t slope = std::max<int32_t>(appliedTorqueSlope, 1);
    int64_t expectedMs = static_cast<int64_t>(std::abs(appliedTorquePerMille)) * 1000 / slope;
    int64_t deadlineUs = esp_timer_get_time() + (expectedMs * 3 / 2 + RAMP_STOP_MARGIN_MS) * 1000;
    appliedTorquePerMille = 0;

    while (esp_timer_get_time() < deadlineUs) {
        // The top bar STOP takes over; the task loop runs the quick stop
        if (isStopRequested) {
            return;
        }

        int32_t velocityRpm = 0;
        sampleMotion(velocityRpm);
        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    // Brake to standstill on the quick-stop ramp, then power off; without it the shaft coasts from speed (TRQ-001)
    stopMotion("Torque mode stopped");
}

bool MotorService::stopMotion(const char* reason, MessageLevel level) {
    // Quick stop: the drive brakes with 6085 and, with 605A = 2, ends in Switch On Disabled (D-008)
    bool isQuickStopSent = writeObject(Objects::CONTROLWORD, Controlword::QUICK_STOP, "Quick stop");
    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(QUICK_STOP_TIMEOUT_MS) * 1000;

    while (isQuickStopSent && esp_timer_get_time() < deadlineUs) {
        uint32_t statusword = 0;
        int32_t velocityRpm = 0;
        bool isRead = sdo.read(Objects::STATUSWORD, statusword) == SdoResult::OK && sampleMotion(velocityRpm);
        DriveState state = decodeDriveState(static_cast<uint16_t>(statusword));
        bool isBraking = state == DriveState::OPERATION_ENABLED || state == DriveState::QUICK_STOP_ACTIVE;

        if (isRead && (!isBraking || std::abs(velocityRpm) < STOPPED_THRESHOLD_RPM)) {
            break;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    // Power stage off in every case: after the quick stop, or straight away if the drive didn't take it (coasting)
    bool isDisabled = writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Power stage off");
    appliedVelocityRpm = 0;
    sdo.write(Objects::TARGET_VELOCITY, 0);

    if (!isQuickStopSent && !isDisabled) {
        // Both writes failed: the motor may still be running, so keep it shown as running with the write's error
        ESP_LOGW(TAG, "Stop failed");

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = false;
    }

    if (isQuickStopSent) {
        setMessage(level, "%s", reason);
    } else {
        setMessage(MessageLevel::ERROR, "%s: quick stop failed, power stage off, motor coasting", reason);
    }

    ESP_LOGI(TAG, "%s", reason);

    return true;
}

void MotorService::rampStop() {
    setMessage(MessageLevel::INFO, "Ramping down");

    // Target 0 decelerates with 6084, which follows the acceleration setting
    if (!writeObject(Objects::TARGET_VELOCITY, 0, "Ramp stop")) {
        stopMotion("Ramp stop failed, quick-stopped", MessageLevel::WARNING);

        return;
    }

    appliedVelocityRpm = 0;

    // Time limit: 1.5× what the deceleration should take from the current speed, plus a margin
    MotorStatus current = getStatus();
    int32_t decelerationRpmPerSecond = std::max(appliedAccelerationRpmPerSecond, MIN_ACCELERATION_RPM_PER_SECOND);
    int64_t expectedMs = static_cast<int64_t>(std::abs(current.velocityRpm)) * 1000 / decelerationRpmPerSecond;
    int64_t deadlineUs = esp_timer_get_time() + (expectedMs * 3 / 2 + RAMP_STOP_MARGIN_MS) * 1000;

    while (true) {
        // The top bar STOP takes over; the task loop runs the quick stop
        if (isStopRequested) {
            return;
        }

        int32_t velocityRpm = 0;

        if (sampleMotion(velocityRpm)) {
            if (std::abs(velocityRpm) < STOPPED_THRESHOLD_RPM) {
                break;
            }
        }

        if (esp_timer_get_time() > deadlineUs) {
            stopMotion("Ramp took too long, quick-stopped", MessageLevel::WARNING);

            return;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    // Standstill: power stage off
    if (!writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Power stage off")) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = false;
    }

    setMessage(MessageLevel::INFO, "Stopped with the ramp");
    ESP_LOGI(TAG, "Ramp stop done");
}

void MotorService::sampleUntilNextPoll() {
    for (uint32_t elapsedMs = 0; elapsedMs < POLL_INTERVAL_MS; elapsedMs += SAMPLE_INTERVAL_MS) {
        if (isStopRequested) {
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        keepAlive();

        // Only with a drive; a failed read just leaves a gap, the poll decides about a lost drive
        int32_t velocityRpm = 0;

        if (getStatus().isConnected) {
            sampleMotion(velocityRpm);
        }
    }
}

bool MotorService::sampleMotion(int32_t& velocityRpm) {
    if (sdo.readSigned(Objects::VELOCITY_ACTUAL, velocityRpm) != SdoResult::OK) {
        return false;
    }

    // A failed position or current read leaves a gap in that line only
    int32_t positionUnits = 0;
    int32_t currentPerMille = 0;
    bool hasPosition = sdo.readSigned(Objects::POSITION_ACTUAL_USER, positionUnits) == SdoResult::OK;
    bool hasCurrent = sdo.readSigned(Objects::CURRENT_ACTUAL, currentPerMille) == SdoResult::OK;
    recordMotionSample(velocityRpm, hasPosition, positionUnits, hasCurrent, currentPerMille);

    return true;
}

void MotorService::recordMotionSample(int32_t velocityRpm, bool hasPosition, int32_t positionUnits, bool hasCurrent, int32_t currentPerMille) {
    std::lock_guard<std::mutex> lock(statusMutex);
    MotionSample sample = {velocityRpm, 0, SAMPLE_NONE, SAMPLE_NONE, SAMPLE_NONE, SAMPLE_NONE, SAMPLE_NONE};

    // Readouts follow the samples too: stops and state walks sample without polling, and the tiles would freeze
    status.velocityRpm = velocityRpm;

    if (hasPosition) {
        setPosition(positionUnits);
    }

    if (hasCurrent) {
        setCurrent(currentPerMille);
        sample.currentCentiamps = status.currentCentiamps;
        sample.currentPerMille = currentPerMille;
    }

    // The torque target, in the current's units: 6071 is per mille of the rated current too (bench 2026-10-12)
    if (status.isRunning && status.motionMode == MotionMode::TORQUE) {
        sample.targetTorquePerMille = appliedTorquePerMille;
    }

    // The velocity target the drive is following: what was last written while running, 0 once stopped
    sample.targetRpm = status.isRunning ? appliedVelocityRpm : 0;

    // Positions from the console zero (D-021); the target only while a set-point is being followed or held
    if (status.unitsPerTurn > 0) {
        if (hasPosition) {
            sample.positionMilliturns = toMilliturns(positionUnits);
        }

        if (status.isRunning && status.motionMode == MotionMode::POSITION && !status.isHalted) {
            sample.targetPositionMilliturns = toMilliturns(sentTargetUnits);
        }
    }

    motionHistory[status.motionSampleCount % MOTION_HISTORY_LENGTH] = sample;
    status.motionSampleCount++;
}

void MotorService::setPosition(int32_t positionUnits) {
    status.positionUnits = positionUnits;

    // Turns from the console zero, in 0.001 turn
    if (status.unitsPerTurn > 0) {
        int64_t unitsFromZero = static_cast<int64_t>(positionUnits) - status.positionZeroUnits;
        status.positionMilliturns = static_cast<int32_t>(unitsFromZero * 1000 / status.unitsPerTurn);
        status.positionDecidegrees = static_cast<int32_t>(unitsFromZero * 3600 / status.unitsPerTurn);

        // A relative move continues from the last target while the drive is moving to it or holding it, so repeated
        // moves land on exact steps; after a halt, release or fault it starts from where the motor is (D4)
        int64_t baseUnits = hasRelativeBase(status) ? sentTargetUnits : positionUnits;
        status.relativeBaseMilliturns = static_cast<int32_t>((baseUnits - status.positionZeroUnits) * 1000 / status.unitsPerTurn);
    }
}

void MotorService::setCurrent(int32_t currentPerMille) {
    status.currentPerMille = currentPerMille;

    // 6078 is per mille of the rated current (bench 2026-10-09, docs/NiMotion motors.md), so amps need 2000:09
    status.currentCentiamps = static_cast<int32_t>(static_cast<int64_t>(currentPerMille) * status.ratedCurrentCentiamps / 1000);

    // Torque from the current: 6071 and 6078 are both per mille of the rated current, so the rated torque scales it
    // (nominal, not measured; the Torque screen shows it as approximate)
    int32_t magnitudePerMille = currentPerMille < 0 ? -currentPerMille : currentPerMille;
    status.torqueDeciMilliNm = static_cast<int32_t>(static_cast<int64_t>(magnitudePerMille) * status.ratedTorqueMilliNm / 100);
}

int32_t MotorService::toMilliturns(int32_t positionUnits) {
    int64_t unitsFromZero = static_cast<int64_t>(positionUnits) - status.positionZeroUnits;

    return static_cast<int32_t>(unitsFromZero * 1000 / status.unitsPerTurn);
}

size_t MotorService::getMotionSamples(uint32_t fromSampleNumber, MotionSample* samples, size_t maxCount, uint32_t& nextSampleNumber) {
    std::lock_guard<std::mutex> lock(statusMutex);
    uint32_t sampleCount = status.motionSampleCount;

    // Skip what the ring buffer no longer holds, and a start number from the future (e.g. after a mix-up)
    uint32_t oldestKept = sampleCount > MOTION_HISTORY_LENGTH ? sampleCount - MOTION_HISTORY_LENGTH : 0;
    uint32_t sampleNumber = fromSampleNumber < oldestKept || fromSampleNumber > sampleCount ? oldestKept : fromSampleNumber;
    size_t copied = 0;

    while (sampleNumber < sampleCount && copied < maxCount) {
        samples[copied++] = motionHistory[sampleNumber % MOTION_HISTORY_LENGTH];
        sampleNumber++;
    }

    nextSampleNumber = sampleNumber;

    return copied;
}

bool MotorService::goPosition() {
    MotorStatus current = getStatus();

    if (!canStartMotion(current)) {
        return false;
    }

    if (current.unitsPerTurn <= 0) {
        setMessage(MessageLevel::ERROR, "GO refused: units per turn unknown (608F, 6091)");

        return false;
    }

    // Another mode in control: normally ended when the screen opened (D-022); end it here too as a guard
    if (current.isRunning && current.motionMode != MotionMode::POSITION) {
        if (!endOtherMode(MotionMode::POSITION, "position GO")) {
            return false;
        }

        current = getStatus();
    }

    // Absolute target in user units: from the zero, or the relative base plus the distance. The drive always gets an
    // absolute target (bit 6 and 60F2 unused, D4); kept inside int32 with a margin (target overflow is alarm 0x8616)
    bool isRelative = isPositionRelative;
    int32_t distanceMilliturns = moveByMilliturns;
    int64_t baseUnits = hasRelativeBase(current) ? sentTargetUnits : current.positionUnits;
    int64_t targetUnits = isRelative ? baseUnits + static_cast<int64_t>(distanceMilliturns) * current.unitsPerTurn / 1000
                                     : current.positionZeroUnits + static_cast<int64_t>(targetPositionMilliturns) * current.unitsPerTurn / 1000;
    targetUnits = std::clamp<int64_t>(targetUnits, INT32_MIN + static_cast<int64_t>(POSITION_LIMIT_MARGIN_UNITS), INT32_MAX - static_cast<int64_t>(POSITION_LIMIT_MARGIN_UNITS));

    // Enter profile position mode if not in it yet; it then holds the current position until the first target
    bool isInPositionMode = current.isRunning && current.motionMode == MotionMode::POSITION && current.state == DriveState::OPERATION_ENABLED;

    if (!isInPositionMode) {
        setMessage(MessageLevel::INFO, "Starting position mode");

        // The manual's order (CN 6.3.6): target, speed and ramps first, then the state walk
        int64_t speedUnitsPerSecond = static_cast<int64_t>(moveSpeedRpm) * current.unitsPerTurn / 60;
        bool isProfileWritten = writeObject(Objects::TARGET_POSITION, static_cast<uint32_t>(static_cast<int32_t>(targetUnits)), "Target position") &&
                                writeObject(Objects::PROFILE_VELOCITY, static_cast<uint32_t>(speedUnitsPerSecond), "Move speed") && applyAcceleration();

        if (!isProfileWritten || !widenPositionLimitsIfNeeded(static_cast<int32_t>(targetUnits)) || !enableOperation(OperationMode::PROFILE_POSITION, "Profile position mode")) {
            return false;
        }

        // A set-point sent within a few ms of Operation Enabled runs without the profile: the motor jumps to the target at
        // up to ~2700 rpm whatever 6081 says (bench 2026-10-09: 7 ms walks jumped, 40 ms walks ramped). Give it time first
        if (!waitAfterEnable()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = true;
        status.motionMode = MotionMode::POSITION;
    } else if (!widenPositionLimitsIfNeeded(static_cast<int32_t>(targetUnits))) {
        return false;
    }

    // Speed and ramps as they are now, then the target
    int32_t speedRpm = moveSpeedRpm;

    if (!applyAcceleration() || !sendSetPoint(static_cast<int32_t>(targetUnits), speedRpm, current.unitsPerTurn)) {
        return false;
    }

    int64_t speedUnitsPerSecond = static_cast<int64_t>(speedRpm) * current.unitsPerTurn / 60;
    int32_t milliturns = static_cast<int32_t>((targetUnits - current.positionZeroUnits) * 1000 / current.unitsPerTurn);

    if (isRelative) {
        setMessage(MessageLevel::INFO,
                   "Moving by %s%" PRId32 ".%03" PRId32 " to %s%" PRId32 ".%03" PRId32 " turns",
                   distanceMilliturns < 0 ? "-" : "+",
                   std::abs(distanceMilliturns) / 1000,
                   std::abs(distanceMilliturns) % 1000,
                   milliturns < 0 ? "-" : "",
                   std::abs(milliturns) / 1000,
                   std::abs(milliturns) % 1000);
    } else {
        setMessage(MessageLevel::INFO, "Moving to %s%" PRId32 ".%03" PRId32 " turns", milliturns < 0 ? "-" : "", std::abs(milliturns) / 1000, std::abs(milliturns) % 1000);
    }
    ESP_LOGI(TAG, "Position target %" PRId64 " units (%" PRId32 " milliturns from zero) at %" PRId64 " units/s", targetUnits, milliturns, speedUnitsPerSecond);

    return true;
}

bool MotorService::hasRelativeBase(const MotorStatus& current) const {
    return current.isRunning && current.motionMode == MotionMode::POSITION && !current.isHalted;
}

bool MotorService::sendSetPoint(int32_t targetUnits, int32_t speedRpm, int32_t unitsPerTurn) {
    // Move speed and target; 6081 is in user units per second
    int64_t speedUnitsPerSecond = static_cast<int64_t>(speedRpm) * unitsPerTurn / 60;

    if (!writeObject(Objects::PROFILE_VELOCITY, static_cast<uint32_t>(speedUnitsPerSecond), "Move speed") ||
        !writeObject(Objects::TARGET_POSITION, static_cast<uint32_t>(targetUnits), "Target position")) {
        return false;
    }

    // Set-point handshake (CN:1452-1455): bit 4 up with bit 5 "change immediately", wait for bit 12, bit 4 down. Bit 8 (halt) stays clear
    const uint16_t enabledWithImmediateChange = Controlword::ENABLE_OPERATION | Controlword::PP_CHANGE_IMMEDIATELY;
    uint32_t statusword = 0;

    if (!writeObject(Objects::CONTROLWORD, enabledWithImmediateChange, "Controlword") ||
        !writeObject(Objects::CONTROLWORD, enabledWithImmediateChange | Controlword::PP_NEW_SET_POINT, "New set-point")) {
        return false;
    }

    bool isAcknowledged = waitForSetPointAcknowledge(statusword);
    writeObject(Objects::CONTROLWORD, enabledWithImmediateChange, "Set-point release");

    if (!isAcknowledged) {
        if (!isStopRequested) {
            setMessage(MessageLevel::ERROR,
                       "The drive didn't take the target: no set-point acknowledge (statusword 0x%04" PRIX32 ", %s)",
                       statusword,
                       getDriveStateName(decodeDriveState(static_cast<uint16_t>(statusword))));
        }

        return false;
    }

    // What the drive is moving with now, so a later speed or ramp change can be re-sent with the same target
    sentTargetUnits = targetUnits;
    sentMoveSpeedRpm = speedRpm;
    sentAccelerationRpmPerSecond = appliedAccelerationRpmPerSecond;
    isAwaitingSettle = true;
    hasLeftTarget = false;

    std::lock_guard<std::mutex> lock(statusMutex);
    status.isHalted = false;
    status.sentTargetUnits = targetUnits;

    return true;
}

void MotorService::followPositionProfile(const MotorStatus& running) {
    int32_t speedRpm = moveSpeedRpm;
    int32_t accelerationRpmPerSecond = targetAccelerationRpmPerSecond;
    int64_t nowUs = esp_timer_get_time();

    // Wait until the values stop changing (steps tapped, a pad), so one re-send covers them all
    if (speedRpm != lastSeenMoveSpeedRpm || accelerationRpmPerSecond != lastSeenAccelerationRpmPerSecond) {
        lastSeenMoveSpeedRpm = speedRpm;
        lastSeenAccelerationRpmPerSecond = accelerationRpmPerSecond;
        lastProfileChangeUs = nowUs;

        return;
    }

    bool isChanged = speedRpm != sentMoveSpeedRpm || accelerationRpmPerSecond != sentAccelerationRpmPerSecond;
    bool isSettled = nowUs - lastProfileChangeUs >= static_cast<int64_t>(PROFILE_CHANGE_SETTLE_MS) * 1000;

    if (!isChanged || !isSettled) {
        return;
    }

    // The drive takes 6081 and the ramps with a set-point, so while moving re-send the same target (D3: still no new
    // place); while holding or halted the values just wait for the next GO
    bool isMoving = !running.isHalted && !(running.statusword & StatuswordBit::TARGET_REACHED);

    if (!applyAcceleration()) {
        return;
    }

    if (isMoving && sendSetPoint(sentTargetUnits, speedRpm, running.unitsPerTurn)) {
        setMessage(MessageLevel::INFO, "Move speed %" PRId32 " rpm, acceleration %" PRId32 " rpm/s applied to the current move", speedRpm, accelerationRpmPerSecond);
        ESP_LOGI(TAG, "Re-sent target %" PRId32 " units with %" PRId32 " rpm, %" PRId32 " rpm/s", sentTargetUnits, speedRpm, accelerationRpmPerSecond);
    } else if (!isMoving) {
        sentMoveSpeedRpm = speedRpm;
        sentAccelerationRpmPerSecond = appliedAccelerationRpmPerSecond;
    }
}

void MotorService::releasePosition() {
    MotorStatus current = getStatus();

    if (!current.isRunning || current.motionMode != MotionMode::POSITION) {
        return;
    }

    // Power stage off: the shaft is free and the drive leaves Operation Enabled
    if (!writeObject(Objects::CONTROLWORD, Controlword::DISABLE_VOLTAGE, "Release")) {
        return;
    }

    isAwaitingSettle = false;

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.isRunning = false;
        status.isHalted = false;
        status.motionMode = MotionMode::NONE;
    }

    setMessage(MessageLevel::INFO, "Released, shaft free");
    ESP_LOGI(TAG, "Position mode released");
}

bool MotorService::endOtherMode(MotionMode wantedMode, const char* reason) {
    MotorStatus current = getStatus();

    if (!current.isRunning || current.motionMode == wantedMode || current.motionMode == MotionMode::NONE) {
        return true;
    }

    bool isEnded = false;
    const char* endedMode = "";

    if (current.motionMode == MotionMode::VELOCITY) {
        endedMode = "Velocity run";
        rampStop();
        isEnded = !getStatus().isRunning;
    } else if (current.motionMode == MotionMode::TORQUE) {
        endedMode = "Torque mode";
        torqueStop();
        isEnded = !getStatus().isRunning;
    } else {
        endedMode = "Position mode";
        isEnded = haltAndRelease();
    }

    // STOP during the ramp or halt has its own message; otherwise say why the motor stopped
    if (isEnded) {
        setMessage(MessageLevel::INFO, "%s ended: %s", endedMode, reason);
        ESP_LOGI(TAG, "%s ended: %s", endedMode, reason);
    }

    return isEnded;
}

bool MotorService::haltAndRelease() {
    MotorStatus current = getStatus();
    haltPosition();

    // Time limit as in rampStop: 1.5x what the deceleration should take from the current speed, plus a margin
    int32_t decelerationRpmPerSecond = std::max(appliedAccelerationRpmPerSecond, MIN_ACCELERATION_RPM_PER_SECOND);
    int64_t expectedMs = static_cast<int64_t>(std::abs(current.velocityRpm)) * 1000 / decelerationRpmPerSecond;
    int64_t deadlineUs = esp_timer_get_time() + (expectedMs * 3 / 2 + RAMP_STOP_MARGIN_MS) * 1000;

    while (true) {
        if (isStopRequested) {
            return false;
        }

        int32_t velocityRpm = 0;

        if (sampleMotion(velocityRpm)) {
            if (std::abs(velocityRpm) < STOPPED_THRESHOLD_RPM) {
                break;
            }
        }

        if (esp_timer_get_time() > deadlineUs) {
            stopMotion("Halt took too long, quick-stopped", MessageLevel::WARNING);

            return !getStatus().isRunning;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }

    releasePosition();

    return !getStatus().isRunning;
}

void MotorService::checkSettled() {
    MotorStatus current = getStatus();

    if (!isAwaitingSettle || !current.isRunning || current.motionMode != MotionMode::POSITION || current.isHalted) {
        return;
    }

    // Bit 10 can still show the previous arrival right after a new set-point, so wait for it to drop first
    bool isTargetReached = current.statusword & StatuswordBit::TARGET_REACHED;

    if (!isTargetReached) {
        hasLeftTarget = true;

        return;
    }

    if (!hasLeftTarget || current.unitsPerTurn <= 0) {
        return;
    }

    // Where it stopped, and how far from the target (within the position window 6067 when it reports reached)
    isAwaitingSettle = false;
    int32_t errorUnits = current.positionUnits - sentTargetUnits;
    int32_t milliturns = current.positionMilliturns;
    setMessage(
        MessageLevel::INFO, "Settled at %s%" PRId32 ".%03" PRId32 " turns, error %+" PRId32 " counts", milliturns < 0 ? "-" : "", std::abs(milliturns) / 1000, std::abs(milliturns) % 1000, errorUnits);
    ESP_LOGI(TAG, "Settled at %" PRId32 " units, error %+" PRId32, current.positionUnits, errorUnits);
}

void MotorService::haltPosition() {
    MotorStatus current = getStatus();

    if (!current.isRunning || current.motionMode != MotionMode::POSITION) {
        return;
    }

    // Halt decelerates on 605D's ramp (default 1: the 6084 deceleration) and keeps holding (CN:918-930)
    if (writeObject(Objects::CONTROLWORD, Controlword::ENABLE_OPERATION | Controlword::PP_CHANGE_IMMEDIATELY | Controlword::HALT, "Halt")) {
        std::lock_guard<std::mutex> lock(statusMutex);
        isAwaitingSettle = false;
        status.isHalted = true;
        snprintf(status.message, sizeof(status.message), "Halted, holding position");
        status.messageLevel = MessageLevel::INFO;
        status.messageSequence++;
    }
}

bool MotorService::widenPositionLimitsIfNeeded(int32_t targetUnits) {
    MotorStatus current = getStatus();

    if (targetUnits >= current.positionLimitMinUnits && targetUnits <= current.positionLimitMaxUnits) {
        return true;
    }

    // Run-time only, never saved; the values the drive came with stay in the status (D-020)
    int32_t widenedMin = INT32_MIN + POSITION_LIMIT_MARGIN_UNITS;
    int32_t widenedMax = INT32_MAX - POSITION_LIMIT_MARGIN_UNITS;

    if (!writeObject(Objects::SOFTWARE_POSITION_LIMIT_MIN, static_cast<uint32_t>(widenedMin), "Widen 607D:01") ||
        !writeObject(Objects::SOFTWARE_POSITION_LIMIT_MAX, static_cast<uint32_t>(widenedMax), "Widen 607D:02")) {
        return false;
    }

    int32_t readMin = current.positionLimitMinUnits;
    int32_t readMax = current.positionLimitMaxUnits;
    sdo.readSigned(Objects::SOFTWARE_POSITION_LIMIT_MIN, readMin);
    sdo.readSigned(Objects::SOFTWARE_POSITION_LIMIT_MAX, readMax);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.positionLimitMinUnits = readMin;
        status.positionLimitMaxUnits = readMax;
    }

    ESP_LOGI(TAG,
             "607D widened from %" PRId32 "..%" PRId32 " to %" PRId32 "..%" PRId32 " (reads back), drive state %s",
             current.originalPositionLimitMinUnits,
             current.originalPositionLimitMaxUnits,
             readMin,
             readMax,
             getDriveStateName(current.state));

    if (targetUnits < readMin || targetUnits > readMax) {
        setMessage(MessageLevel::WARNING, "607D write accepted, but it reads back %" PRId32 "..%" PRId32 ", target outside", readMin, readMax);

        return false;
    }

    return true;
}

bool MotorService::waitAfterEnable() {
    int64_t endUs = esp_timer_get_time() + static_cast<int64_t>(POSITION_ENABLE_SETTLE_MS) * 1000;

    while (esp_timer_get_time() < endUs) {
        if (isStopRequested) {
            return false;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return true;
}

bool MotorService::waitForSetPointAcknowledge(uint32_t& statusword) {
    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(SET_POINT_TIMEOUT_MS) * 1000;

    while (esp_timer_get_time() < deadlineUs) {
        if (isStopRequested) {
            return false;
        }

        if (sdo.read(Objects::STATUSWORD, statusword) == SdoResult::OK && (statusword & StatuswordBit::SET_POINT_ACKNOWLEDGE)) {
            return true;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    return false;
}

bool MotorService::applyAcceleration() {
    int32_t accelerationRpmPerSecond = targetAccelerationRpmPerSecond;
    uint32_t countsPerSecondSquared = static_cast<uint32_t>(rpmToCountsPerSecond(accelerationRpmPerSecond));

    if (!writeObject(Objects::PROFILE_ACCELERATION, countsPerSecondSquared, "Profile acceleration") || !writeObject(Objects::PROFILE_DECELERATION, countsPerSecondSquared, "Profile deceleration")) {
        return false;
    }

    appliedAccelerationRpmPerSecond = accelerationRpmPerSecond;

    return true;
}

bool MotorService::stepTo(uint16_t controlword, DriveState expectedState, const char* stepName) {
    // A stop request cancels the start sequence; the task loop then runs the stop
    if (isStopRequested) {
        setMessage(MessageLevel::INFO, "Start cancelled");

        return false;
    }

    if (!writeObject(Objects::CONTROLWORD, controlword, stepName)) {
        return false;
    }

    int64_t deadlineUs = esp_timer_get_time() + static_cast<int64_t>(STATE_TIMEOUT_MS) * 1000;
    uint32_t statusword = 0;

    while (esp_timer_get_time() < deadlineUs) {
        if (sdo.read(Objects::STATUSWORD, statusword) == SdoResult::OK && decodeDriveState(static_cast<uint16_t>(statusword)) == expectedState) {
            return true;
        }

        // Keep sampling velocity while waiting, so motion during the state walk shows on charts and in diagnostics
        int32_t velocityRpm = 0;

        sampleMotion(velocityRpm);

        if (isStopRequested) {
            setMessage(MessageLevel::INFO, "Start cancelled");

            return false;
        }

        keepAlive();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint32_t errorCode = 0;
    sdo.read(Objects::ERROR_CODE, errorCode);

    setMessage(MessageLevel::ERROR,
               "Step \"%s\" timed out in state %s (statusword 0x%04" PRIX32 ", error 0x%04" PRIX32 ")",
               stepName,
               getDriveStateName(decodeDriveState(static_cast<uint16_t>(statusword))),
               statusword,
               errorCode);
    ESP_LOGW(TAG, "%s", getStatus().message);

    return false;
}

bool MotorService::writeObject(const ObjectEntry& entry, uint32_t value, const char* stepName) {
    SdoResult result = sdo.write(entry, value);

    if (result == SdoResult::OK) {
        return true;
    }

    if (result == SdoResult::ABORTED) {
        setMessage(
            MessageLevel::ERROR, "%s: write %04X:%02X aborted 0x%08" PRIX32 " (%s)", stepName, entry.index, entry.subIndex, sdo.lastAbortCode, SdoClient::getAbortCodeDescription(sdo.lastAbortCode));
    } else {
        setMessage(MessageLevel::ERROR, "%s: write %04X:%02X failed, %s", stepName, entry.index, entry.subIndex, SdoClient::getResultName(result));
    }

    ESP_LOGW(TAG, "%s", getStatus().message);

    return false;
}

void MotorService::keepAlive() {
    esp_task_wdt_reset();

    if (watchdogMs == 0) {
        return;
    }

    // Feed the drive at a third of its timeout to leave room for a slow SDO round trip; a failed feed is retried on the
    // next call rather than a full interval later
    int64_t nowUs = esp_timer_get_time();
    int64_t feedIntervalUs = static_cast<int64_t>(watchdogMs) * 1000 / 3;

    if (nowUs - lastWatchdogFeedUs < feedIntervalUs) {
        return;
    }

    // Manual: writing 200C:1B again is the "dog feeding" command
    if (sdo.write(Objects::COMMUNICATION_WATCHDOG_MS, watchdogMs) == SdoResult::OK) {
        lastWatchdogFeedUs = nowUs;
    }
}

void MotorService::handleFrame(const CanFrame& frame) {
    // CiA 301 EMCY: error code (2 bytes), error register, 5 manufacturer bytes
    if (frame.id != EMCY_COB_ID + sdo.getNodeId() || frame.length < 3) {
        return;
    }

    uint16_t errorCode = static_cast<uint16_t>(frame.data[0] | (frame.data[1] << 8));
    uint8_t errorRegister = frame.data[2];

    // The alarm behind a drive that switches itself off: the clearing EMCY (0x0000) often follows at once
    if (errorCode != 0) {
        lastAlarmCode = errorCode;
        lastAlarmUs = esp_timer_get_time();
    }

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.emcyErrorCode = errorCode;
        status.emcyErrorRegister = errorRegister;
        status.emcySequence++;

        SessionLogEntry entry;
        entry.kind = errorCode != 0 ? SessionLogEntry::Kind::ALARM : SessionLogEntry::Kind::CLEARED;
        entry.code = errorCode;
        entry.errorRegister = errorRegister;
        addSessionLogEntry(entry);
    }

    // Read after the transfer in progress; SDO can't be used from here
    isFaultRecordStale = true;

    ESP_LOGW(TAG, "EMCY 0x%04X, error register 0x%02X", errorCode, errorRegister);

    // The pause of a flash write raises 0x7501; it's expected, so it doesn't replace the instruction on screen
    bool isFlashWritePause = errorCode == AlarmCodes::COMMUNICATION_FAILURE && lastFlashWriteUs >= 0 && esp_timer_get_time() - lastFlashWriteUs < static_cast<int64_t>(FLASH_WRITE_QUIET_MS) * 1000;

    // Code 0 means the error is gone
    if (errorCode != 0 && !isFlashWritePause) {
        const AlarmCode* alarm = AlarmCodes::find(errorCode);
        setMessage(MessageLevel::WARNING, "Drive reported 0x%04X %s (EMCY, error register 0x%02X)", errorCode, alarm != nullptr ? alarm->name : "(unknown code)", errorRegister);
    }
}

void MotorService::setMessage(MessageLevel level, const char* format, ...) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status.messageLevel = level;

    va_list arguments;
    va_start(arguments, format);
    vsnprintf(status.message, sizeof(status.message), format, arguments);
    status.messageSequence++;
    va_end(arguments);
}

int32_t MotorService::rpmToCountsPerSecond(int32_t velocityRpm) const {
    int64_t countsPerSecond = static_cast<int64_t>(velocityRpm) * encoderIncrements / 60;

    return static_cast<int32_t>(countsPerSecond);
}
